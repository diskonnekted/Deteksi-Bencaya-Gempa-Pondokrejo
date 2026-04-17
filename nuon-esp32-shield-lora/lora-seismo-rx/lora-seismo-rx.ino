/*********
  LoRa Earthquake Seismograph - Receiver
  Sends data to PC via UDP
*********/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <LoRa.h>

const char* defaultSsid = "BARU";
const char* defaultPassword = "12345678";

const char* apSsid = "Orion x64";

const bool enableHttpPost = true;
const char* postUrl = "http://192.168.1.66:18080/api/sensor";
const char* postApiKey = "";

const uint8_t relayPin = 26;
const bool relayActiveLow = true;
const unsigned long relayHoldMs = 15000;
unsigned long relayActiveUntilMs = 0;
bool relayOn = false;

// PC IP (your computer)
const char* pcIP = "192.168.1.66";
const uint16_t pcPort = 8888;

// LoRa pins (NUON Shield)
#define LORA_NSS 16
#define LORA_RST 13
#define LORA_DIO0 17

// UDP client
WiFiUDP udp;
WebServer web(80);
Preferences prefs;
WiFiClientSecure secureClient;

String wifiSsid = "";
String wifiPassword = "";
bool apRunning = false;
unsigned long lastWifiAttemptMs = 0;
unsigned long wifiFailStartMs = 0;

const unsigned long wifiRetryIntervalMs = 15000;
const unsigned long apReenableAfterMs = 10UL * 60UL * 1000UL;
const unsigned long httpPostMinIntervalMs = 400;
unsigned long lastHttpPostMs = 0;

bool startsWith(const String& s, const char* prefix) {
  return s.startsWith(prefix);
}

void setRelay(bool on) {
  relayOn = on;
  uint8_t level = relayActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
  digitalWrite(relayPin, level);
}

void updateRelay() {
  bool shouldOn = millis() < relayActiveUntilMs;
  if (shouldOn != relayOn) setRelay(shouldOn);
}

void postJson(const String& body) {
  if (!enableHttpPost) return;
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastHttpPostMs < httpPostMinIntervalMs) return;
  lastHttpPostMs = now;

  String url = postUrl;
  if (url.length() == 0) return;

  HTTPClient http;
  http.setTimeout(2500);

  bool ok = false;
  if (startsWith(url, "https://")) {
    secureClient.setInsecure();
    ok = http.begin(secureClient, url);
  } else {
    ok = http.begin(url);
  }
  if (!ok) {
    static unsigned long lastBeginFailMs = 0;
    if (now - lastBeginFailMs > 2000) {
      lastBeginFailMs = now;
      Serial.print("HTTP begin failed url=");
      Serial.println(url);
    }
    return;
  }

  http.addHeader("Content-Type", "application/json");
  if (String(postApiKey).length() > 0) {
    http.addHeader("X-API-Key", postApiKey);
  }

  int code = http.POST((uint8_t*)body.c_str(), body.length());
  String err = http.errorToString(code);
  http.end();
  if (code <= 0) {
    Serial.print("HTTP POST failed: ");
    Serial.print(code);
    Serial.print(" ");
    Serial.println(err);
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("GW: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("URL: ");
    Serial.println(url);
  } else if (code >= 400) {
    Serial.print("HTTP POST status: ");
    Serial.println(code);
  }
}

void startSoftAP() {
  if (apRunning) return;
  WiFi.mode(WIFI_AP_STA);
  IPAddress ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gateway, subnet);
  WiFi.softAP(apSsid);
  apRunning = true;
  Serial.print("SoftAP ON: ");
  Serial.print(apSsid);
  Serial.print(" IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopSoftAP() {
  if (!apRunning) return;
  WiFi.softAPdisconnect(true);
  apRunning = false;
  WiFi.mode(WIFI_STA);
  Serial.println("SoftAP OFF");
}

void loadWifiCreds() {
  prefs.begin("wifi", true);
  wifiSsid = prefs.getString("ssid", defaultSsid);
  wifiPassword = prefs.getString("pass", defaultPassword);
  prefs.end();
}

void saveWifiCreds(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  wifiSsid = ssid;
  wifiPassword = pass;
}

void connectWifi(bool force = false) {
  if (!force && (millis() - lastWifiAttemptMs) < wifiRetryIntervalMs) return;
  lastWifiAttemptMs = millis();
  if (wifiFailStartMs == 0) wifiFailStartMs = millis();

  if (wifiSsid.length() == 0) return;

  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(apRunning ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  Serial.print("Connecting WiFi SSID: ");
  Serial.println(wifiSsid);
}

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

void handleRoot() {
  String page;
  page.reserve(6000);
  page += F("<!doctype html><html><head><meta charset='utf-8'/>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'/>");
  page += F("<title>Orion x64 WiFi Setup</title>");
  page += F("<style>body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:0;background:#0b0f18;color:#e8ecf3}main{max-width:740px;margin:0 auto;padding:18px}h1{font-size:18px;margin:0 0 8px}p{color:#a6afbf}fieldset{border:1px solid #202532;border-radius:12px;padding:14px;background:rgba(10,12,17,.7)}label{display:block;margin:10px 0 6px;color:#a6afbf;font-size:12px}select,input{width:100%;padding:10px 12px;border-radius:10px;border:1px solid #202532;background:rgba(6,7,10,.6);color:#e8ecf3;outline:none}button{margin-top:12px;padding:10px 12px;border-radius:10px;border:1px solid rgba(0,211,255,.3);background:rgba(0,211,255,.14);color:#e8ecf3;font-weight:600;cursor:pointer}code{background:rgba(6,7,10,.6);padding:2px 6px;border-radius:6px;border:1px solid #202532}</style>");
  page += F("</head><body><main>");
  page += F("<h1>Orion x64 WiFi Setup</h1>");

  wl_status_t st = WiFi.status();
  page += F("<p>Status: ");
  if (st == WL_CONNECTED) page += F("<b>CONNECTED</b>");
  else page += F("<b>DISCONNECTED</b>");
  page += F(" | STA IP: <code>");
  page += WiFi.localIP().toString();
  page += F("</code> | AP IP: <code>");
  page += WiFi.softAPIP().toString();
  page += F("</code></p>");

  int n = WiFi.scanNetworks(false, true);
  page += F("<fieldset><form method='POST' action='/save'>");
  page += F("<label>Jaringan WiFi</label><select name='ssid' required>");
  if (n <= 0) {
    page += F("<option value=''>Tidak ada jaringan</option>");
  } else {
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      page += F("<option value='");
      page += htmlEscape(s);
      page += F("'");
      if (s == wifiSsid) page += F(" selected");
      page += F(">");
      page += htmlEscape(s);
      page += F(" | ");
      page += String(rssi);
      page += F(" dBm");
      page += secured ? F(" | secured") : F(" | open");
      page += F("</option>");
    }
  }
  page += F("</select>");
  page += F("<label>Password</label><input name='pass' type='password' placeholder='(boleh kosong jika open)'/>");
  page += F("<button type='submit'>Simpan & Konek</button>");
  page += F("</form></fieldset>");
  page += F("</main></body></html>");

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html", page);
}

void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"";
    json += htmlEscape(WiFi.SSID(i));
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
    json += "}";
  }
  json += "]";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", json);
}

void handleSave() {
  if (!web.hasArg("ssid")) {
    web.send(400, "text/plain", "Bad Request");
    return;
  }
  String s = web.arg("ssid");
  String p = web.hasArg("pass") ? web.arg("pass") : "";
  s.trim();
  p.trim();
  saveWifiCreds(s, p);
  wifiFailStartMs = millis();
  connectWifi(true);
  web.sendHeader("Location", "/", true);
  web.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);

  pinMode(relayPin, OUTPUT);
  setRelay(false);

  // Init LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  while (!LoRa.begin(920E6)) {
    Serial.println("LoRa init failed!");
    delay(500);
  }
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Ready!");

  loadWifiCreds();
  startSoftAP();
  connectWifi(true);

  web.on("/", HTTP_GET, handleRoot);
  web.on("/scan", HTTP_GET, handleScan);
  web.on("/save", HTTP_POST, handleSave);
  web.begin();

  // Init UDP
  udp.begin(0); // Auto port
  Serial.println("UDP ready!");
}

String getMMIDescription(int mmi) {
  switch (mmi) {
    case 1:  return "Tidak terasa";
    case 2:  return "Terasa sangat lemah";
    case 3:  return "Lemah - seperti truk lewat";
    case 4:  return "Sedang - jendela bergetar";
    case 5:  return "Agak kuat - benda bergerak";
    case 6:  return "Kuat - terasa semua orang";
    case 7:  return "Sangat kuat - sulit berdiri";
    case 8:  return "Merusak - bangunan retak";
    case 9:  return "Hancur - bangunan rubuh";
    case 10: return "Bencana - struktur hancur";
    case 11: return "Bencana total";
    case 12: return "Kehancuran total";
    default: return "-";
  }
}

void loop() {
  web.handleClient();
  updateRelay();

  if (WiFi.status() == WL_CONNECTED) {
    wifiFailStartMs = 0;
    if (apRunning) stopSoftAP();
  } else {
    connectWifi(false);
    if (!apRunning && wifiFailStartMs > 0 && (millis() - wifiFailStartMs) >= apReenableAfterMs) {
      startSoftAP();
    }
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String data = LoRa.readString();
    int rssi = LoRa.packetRssi();

    // Parse: ax,ay,az,peak,mmi
    int pos1 = data.indexOf(',');
    float ax = data.substring(0, pos1).toFloat();

    int pos2 = data.indexOf(',', pos1 + 1);
    float ay = data.substring(pos1 + 1, pos2).toFloat();

    int pos3 = data.indexOf(',', pos2 + 1);
    float az = data.substring(pos2 + 1, pos3).toFloat();

    int pos4 = data.indexOf(',', pos3 + 1);
    float peak = data.substring(pos3 + 1, pos4).toFloat();

    int vib = 0;
    int pos5 = data.indexOf(',', pos4 + 1);
    int mmi = 1;
    if (pos5 > 0) {
      mmi = data.substring(pos4 + 1, pos5).toInt();
      vib = data.substring(pos5 + 1).toInt();
    } else {
      mmi = data.substring(pos4 + 1).toInt();
    }

    float mag = sqrt(ax * ax + ay * ay + az * az);

    // Format: ax,ay,az,peak,mmi,rssi
    String udpData = String(ax, 3) + "," +
                     String(ay, 3) + "," +
                     String(az, 3) + "," +
                     String(peak, 3) + "," +
                     String(mmi) + "," +
                     String(rssi) + "," +
                     String(vib);

    // Send to PC via UDP
    if (WiFi.status() == WL_CONNECTED) {
      udp.beginPacket(pcIP, pcPort);
      udp.print(udpData);
      udp.endPacket();
    }

    String json = "{";
    json += "\"ax\":" + String(ax, 3) + ",";
    json += "\"ay\":" + String(ay, 3) + ",";
    json += "\"az\":" + String(az, 3) + ",";
    json += "\"peak\":" + String(peak, 3) + ",";
    json += "\"mmi\":" + String(mmi) + ",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"vib\":" + String(vib);
    json += "}";
    postJson(json);

    if (mmi >= 5) {
      relayActiveUntilMs = millis() + relayHoldMs;
      updateRelay();
    }

    Serial.print("Mag=");
    Serial.print(mag, 3);
    Serial.print(" Peak=");
    Serial.print(peak, 3);
    Serial.print(" MMI=");
    Serial.print(mmi);
    Serial.print(" (");
    Serial.print(getMMIDescription(mmi));
    Serial.print(") RSSI=");
    Serial.print(rssi);
    Serial.print(" Vib=");
    Serial.println(vib);
  }
}

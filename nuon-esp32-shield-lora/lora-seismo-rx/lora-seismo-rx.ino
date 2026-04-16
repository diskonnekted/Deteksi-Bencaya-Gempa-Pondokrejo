/*********
  LoRa Earthquake Seismograph - Receiver
  Sends data to PC via UDP
*********/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <LoRa.h>

// WiFi credentials
const char* ssid = "BARU";
const char* password = "12345678";

// PC IP (your computer)
const char* pcIP = "192.168.1.66";
const uint16_t pcPort = 8888;

// LoRa pins (NUON Shield)
#define LORA_NSS 16
#define LORA_RST 13
#define LORA_DIO0 17

// UDP client
WiFiUDP udp;

void setup() {
  Serial.begin(115200);

  // Init LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  while (!LoRa.begin(920E6)) {
    Serial.println("LoRa init failed!");
    delay(500);
  }
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Ready!");

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Sending data to PC: ");
  Serial.println(pcIP);

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
    udp.beginPacket(pcIP, pcPort);
    udp.print(udpData);
    udp.endPacket();

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

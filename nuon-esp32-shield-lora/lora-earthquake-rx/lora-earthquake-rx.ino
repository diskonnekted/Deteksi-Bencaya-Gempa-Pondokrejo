/*********
  LoRa Earthquake/Vibration Detector - Receiver
  Displays vibration alerts and status
*********/

#include <SPI.h>
#include <LoRa.h>

// LoRa pins (NUON Shield)
#define LORA_NSS 16
#define LORA_RST 13
#define LORA_DIO0 17

void setup() {
  Serial.begin(115200);

  // Init LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  while (!LoRa.begin(920E6)) {
    Serial.println("LoRa init failed!");
    delay(500);
  }
  LoRa.setSyncWord(0xF3);
  Serial.println("Earthquake Monitor Receiver");
  Serial.println("Waiting for data...");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String data = LoRa.readString();
    int rssi = LoRa.packetRssi();

    // Parse data
    int firstComma = data.indexOf(',');
    String type = data.substring(0, firstComma);

    if (type == "ALERT") {
      // Parse: ALERT,ax,ay,az,mag
      int pos1 = firstComma + 1;
      int pos2 = data.indexOf(',', pos1);
      float ax = data.substring(pos1, pos2).toFloat();

      int pos3 = data.indexOf(',', pos2 + 1);
      float ay = data.substring(pos2 + 1, pos3).toFloat();

      int pos4 = data.indexOf(',', pos3 + 1);
      float az = data.substring(pos3 + 1, pos4).toFloat();

      float mag = data.substring(pos4 + 1).toFloat();

      Serial.println("╔══════════════════════════════════════╗");
      Serial.println("║   ⚠️  VIBRATION / EARTHQUAKE!  ⚠️    ║");
      Serial.println("╠══════════════════════════════════════╣");
      Serial.print("║  Magnitude : ");
      Serial.print(mag);
      Serial.println(" m/s²         ║");
      Serial.print("║  X-Axis    : ");
      Serial.print(ax);
      Serial.println(" m/s²           ║");
      Serial.print("║  Y-Axis    : ");
      Serial.print(ay);
      Serial.println(" m/s²           ║");
      Serial.print("║  Z-Axis    : ");
      Serial.print(az);
      Serial.println(" m/s²           ║");
      Serial.print("║  RSSI      : ");
      Serial.print(rssi);
      Serial.println(" dBm             ║");
      Serial.println("╚══════════════════════════════════════╝");
      Serial.println();

    } else if (type == "OK") {
      // Parse: OK,mag
      float mag = data.substring(firstComma + 1).toFloat();
      Serial.print("[OK] Normal | Mag=");
      Serial.print(mag);
      Serial.print(" m/s² | RSSI=");
      Serial.print(rssi);
      Serial.println(" dBm");
    }
  }
}

/*********
  LoRa P2P Receiver
  Receives messages from lora-p2p-transmitter
*********/

#include <SPI.h>
#include <LoRa.h>

//define the pins used by the transceiver module
#define LORA_AURORA_V2_NSS 16
#define LORA_AURORA_V2_RST 13
#define LORA_AURORA_V2_DIO0 17

void setup() {

  //initialize Serial Monitor
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LoRa Receiver");

  //setup LoRa transceiver module
  LoRa.setPins(LORA_AURORA_V2_NSS, LORA_AURORA_V2_RST, LORA_AURORA_V2_DIO0);

  //replace the LoRa.begin(---E-) argument with your location's frequency
  //433E6 for Asia
  //866E6 for Europe
  //915E6 for North America
  while (!LoRa.begin(920E6)) {
    Serial.println(".");
    delay(500);
  }

  // Change sync word (0xF3) to match the transmitter
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Initializing OK!");
}

void loop() {

  // try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    Serial.print("Received packet: '");

    // read packet
    while (LoRa.available()) {
      String data = LoRa.readString();
      Serial.print(data);
    }

    // print RSSI of packet
    Serial.print("' with RSSI ");
    Serial.println(LoRa.packetRssi());
  }
}

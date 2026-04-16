/*********
  LoRa Earthquake/Vibration Detector - Transmitter
  Sensor: MPU6050 (Accelerometer + Gyroscope)
  No external library needed - uses raw I2C
*********/

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>

// LoRa pins (NUON Shield)
#define LORA_NSS 16
#define LORA_RST 13
#define LORA_DIO0 17

// MPU6050 I2C address
#define MPU6050_ADDR 0x68

// MPU6050 registers
#define MPU6050_SMPLRT_DIV   0x19
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_ACCEL_YOUT_H 0x3D
#define MPU6050_ACCEL_ZOUT_H 0x3F
#define MPU6050_TEMP_OUT_H   0x41
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_WHO_AM_I     0x75

// Vibration detection threshold (m/s²)
#define VIBRATION_THRESHOLD 2.0

// Calibration offsets
int16_t axOffset = 0, ayOffset = 0, azOffset = 0;
float accelScale = 9.81 / 16384.0; // 2G range: 16384 LSB/g

void i2cWrite(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t i2cRead(uint8_t reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, (uint8_t)1);
  return Wire.read();
}

int16_t i2cRead16(uint8_t reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, (uint8_t)2);
  return (int16_t)(Wire.read() << 8 | Wire.read());
}

void setup() {
  Serial.begin(115200);

  // Init I2C
  Wire.begin(21, 22);

  // Check MPU6050
  uint8_t whoAmI = i2cRead(MPU6050_WHO_AM_I);
  while (whoAmI != 0x68) {
    Serial.print("MPU6050 not found! WHO_AM_I=0x");
    Serial.println(whoAmI, HEX);
    delay(1000);
    whoAmI = i2cRead(MPU6050_WHO_AM_I);
  }
  Serial.println("MPU6050 detected!");

  // Initialize MPU6050
  i2cWrite(MPU6050_PWR_MGMT_1, 0x00);     // Wake up
  delay(100);
  i2cWrite(MPU6050_SMPLRT_DIV, 0x07);     // 1kHz / (7+1) = 125 Hz
  i2cWrite(MPU6050_CONFIG, 0x00);         // DLPF off
  i2cWrite(MPU6050_GYRO_CONFIG, 0x00);    // ±250 deg/s
  i2cWrite(MPU6050_ACCEL_CONFIG, 0x00);   // ±2G

  // Calibrate (keep device still for 2 seconds)
  Serial.println("Calibrating... Keep device still!");
  delay(2000);
  calibrate();
  Serial.println("Calibration done!");

  // Init LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  while (!LoRa.begin(920E6)) {
    Serial.println("LoRa init failed!");
    delay(500);
  }
  LoRa.setSyncWord(0xF3);
  Serial.println("Earthquake Detector Ready");
}

void calibrate() {
  long axSum = 0, aySum = 0, azSum = 0;
  for (int i = 0; i < 100; i++) {
    axSum += i2cRead16(MPU6050_ACCEL_XOUT_H);
    aySum += i2cRead16(MPU6050_ACCEL_YOUT_H);
    azSum += i2cRead16(MPU6050_ACCEL_ZOUT_H);
    delay(10);
  }
  axOffset = axSum / 100;
  ayOffset = aySum / 100;
  azOffset = azSum / 100;
  Serial.printf("Offsets: X=%d Y=%d Z=%d\n", axOffset, ayOffset, azOffset);
}

void loop() {
  // Read accelerometer
  int16_t axRaw = i2cRead16(MPU6050_ACCEL_XOUT_H);
  int16_t ayRaw = i2cRead16(MPU6050_ACCEL_YOUT_H);
  int16_t azRaw = i2cRead16(MPU6050_ACCEL_ZOUT_H);

  // Convert to m/s² with calibration
  float ax = (axRaw - axOffset) * accelScale;
  float ay = (ayRaw - ayOffset) * accelScale;
  float az = (azRaw - azOffset) * accelScale;

  // Calculate total acceleration magnitude
  float magnitude = sqrt(ax * ax + ay * ay + az * az);

  // Check for vibration/earthquake
  if (magnitude > VIBRATION_THRESHOLD) {
    // Format: "ALERT,ax,ay,az,mag"
    String data = "ALERT," +
                  String(ax, 2) + "," +
                  String(ay, 2) + "," +
                  String(az, 2) + "," +
                  String(magnitude, 2);

    // Send via LoRa
    LoRa.beginPacket();
    LoRa.print(data);
    LoRa.endPacket();

    Serial.print("[!] VIBRATION DETECTED! Mag=");
    Serial.print(magnitude);
    Serial.print(" m/s² | AX=");
    Serial.print(ax);
    Serial.print(" AY=");
    Serial.print(ay);
    Serial.print(" AZ=");
    Serial.println(az);
  } else {
    // Send heartbeat every 10 seconds
    static unsigned long lastBeat = 0;
    if (millis() - lastBeat > 10000) {
      String data = "OK," + String(magnitude, 2);
      LoRa.beginPacket();
      LoRa.print(data);
      LoRa.endPacket();

      Serial.print("[.] Normal | Mag=");
      Serial.println(magnitude);
      lastBeat = millis();
    }
  }

  delay(100); // 10 Hz sampling
}

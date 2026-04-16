/*********
  LoRa Earthquake Seismograph - Transmitter
  Sensor: MPU6050 + MMI Scale Calculation
*********/

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>

// LoRa pins (NUON Shield)
#define LORA_NSS 16
#define LORA_RST 13
#define LORA_DIO0 17

#define VIB_DO_PIN 27
#define VIB_ACTIVE_LOW 1

#if VIB_ACTIVE_LOW
#define VIB_PIN_MODE INPUT_PULLUP
#define VIB_INTERRUPT_EDGE FALLING
#else
#define VIB_PIN_MODE INPUT
#define VIB_INTERRUPT_EDGE RISING
#endif

volatile uint32_t vibPulseCount = 0;
volatile uint32_t vibLastMicros = 0;
uint16_t vibRate = 0;
unsigned long vibWindowStart = 0;

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

float accelScale = 9.81 / 16384.0; // 2G range: 16384 LSB/g

float gravityX = 0.0;
float gravityY = 0.0;
float gravityZ = 0.0;

float smoothedLinMag = 0.0;
int stationaryCount = 0;

// Rolling window for peak calculation (10 samples = 1 second at 10Hz)
#define WINDOW_SIZE 10
float peakWindow[WINDOW_SIZE] = {0};
int peakIndex = 0;

// Baseline noise level (calibrated during idle)
float baselineNoise = 0.0;

// Calibration samples (500 samples = 5 seconds at 10Hz)
#define CAL_SAMPLES 500
#define NOISE_SAMPLES 200

void IRAM_ATTR vibIsr() {
  uint32_t now = micros();
  if (now - vibLastMicros > 2000) {
    vibPulseCount++;
    vibLastMicros = now;
  }
}

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

// Convert m/s² to MMI (Modified Mercalli Intensity)
// Using piecewise mapping optimized for handheld sensor demo
// Note: Hand shaking creates much higher acceleration than earthquake ground motion
int calculateMMI(float magnitude) {
  if (magnitude < 0.1)   return 1;   // Tidak terasa
  if (magnitude < 0.3)   return 2;   // Terasa sangat lemah
  if (magnitude < 0.5)   return 3;   // Lemah - seperti truk lewat
  if (magnitude < 1.0)   return 4;   // Sedang - jendela bergetar
  if (magnitude < 2.0)   return 5;   // Agak kuat - benda bergerak
  if (magnitude < 3.0)   return 6;   // Kuat - terasa semua orang
  if (magnitude < 5.0)   return 7;   // Sangat kuat - sulit berdiri
  if (magnitude < 7.0)   return 8;   // Merusak - bangunan retak
  if (magnitude < 10.0)  return 9;   // Hancur - bangunan rubuh
  if (magnitude < 15.0)  return 10;  // Bencana - struktur hancur
  if (magnitude < 20.0)  return 11;  // Bencana total
  return 12;                           // Kehancuran total
}

// Get MMI description
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
    default: return "Unknown";
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(VIB_DO_PIN, VIB_PIN_MODE);
  attachInterrupt(digitalPinToInterrupt(VIB_DO_PIN), vibIsr, VIB_INTERRUPT_EDGE);
  vibWindowStart = millis();

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

  // Calibrate (keep device still for 5 seconds)
  Serial.println("Calibrating... Keep device still!");
  delay(1000);
  calibrate();
  Serial.println("Calibration done!");

  // Init LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  while (!LoRa.begin(920E6)) {
    Serial.println("LoRa init failed!");
    delay(500);
  }
  LoRa.setSyncWord(0xF3);
  Serial.println("Seismograph Transmitter Ready");
}

void calibrate() {
  long axSum = 0, aySum = 0, azSum = 0;

  for (int i = 0; i < CAL_SAMPLES; i++) {
    int16_t axRaw = i2cRead16(MPU6050_ACCEL_XOUT_H);
    int16_t ayRaw = i2cRead16(MPU6050_ACCEL_YOUT_H);
    int16_t azRaw = i2cRead16(MPU6050_ACCEL_ZOUT_H);

    axSum += axRaw;
    aySum += ayRaw;
    azSum += azRaw;

    delay(10);
  }

  gravityX = (axSum / (float)CAL_SAMPLES) * accelScale;
  gravityY = (aySum / (float)CAL_SAMPLES) * accelScale;
  gravityZ = (azSum / (float)CAL_SAMPLES) * accelScale;

  float noiseSum = 0.0;
  for (int i = 0; i < NOISE_SAMPLES; i++) {
    int16_t axRaw = i2cRead16(MPU6050_ACCEL_XOUT_H);
    int16_t ayRaw = i2cRead16(MPU6050_ACCEL_YOUT_H);
    int16_t azRaw = i2cRead16(MPU6050_ACCEL_ZOUT_H);

    float ax = axRaw * accelScale;
    float ay = ayRaw * accelScale;
    float az = azRaw * accelScale;

    float linX = ax - gravityX;
    float linY = ay - gravityY;
    float linZ = az - gravityZ;
    float linMag = sqrt(linX * linX + linY * linY + linZ * linZ);
    noiseSum += linMag;
    delay(10);
  }

  baselineNoise = noiseSum / (float)NOISE_SAMPLES;
  smoothedLinMag = 0.0;
  stationaryCount = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) peakWindow[i] = 0.0;
  peakIndex = 0;

  Serial.printf("Gravity: X=%.4f Y=%.4f Z=%.4f m/s²\n", gravityX, gravityY, gravityZ);
  Serial.printf("Baseline noise: %.4f m/s²\n", baselineNoise);
}

void loop() {
  unsigned long nowMs = millis();
  if (nowMs - vibWindowStart >= 1000) {
    uint32_t count;
    noInterrupts();
    count = vibPulseCount;
    vibPulseCount = 0;
    interrupts();
    vibRate = (uint16_t)min(count, (uint32_t)65535);
    vibWindowStart += 1000;
  }

  // Read accelerometer
  int16_t axRaw = i2cRead16(MPU6050_ACCEL_XOUT_H);
  int16_t ayRaw = i2cRead16(MPU6050_ACCEL_YOUT_H);
  int16_t azRaw = i2cRead16(MPU6050_ACCEL_ZOUT_H);

  float ax = axRaw * accelScale;
  float ay = ayRaw * accelScale;
  float az = azRaw * accelScale;

  float alpha = 0.02;

  float linX0 = ax - gravityX;
  float linY0 = ay - gravityY;
  float linZ0 = az - gravityZ;
  float linMag0 = sqrt(linX0 * linX0 + linY0 * linY0 + linZ0 * linZ0);

  smoothedLinMag = smoothedLinMag * 0.9 + linMag0 * 0.1;
  float stationaryThreshold = max(0.05f, baselineNoise * 2.0f);
  if (smoothedLinMag < stationaryThreshold) {
    stationaryCount = min(stationaryCount + 1, 2000);
  } else {
    stationaryCount = 0;
  }

  if (stationaryCount >= 30) alpha = 0.2;

  gravityX = gravityX * (1.0 - alpha) + ax * alpha;
  gravityY = gravityY * (1.0 - alpha) + ay * alpha;
  gravityZ = gravityZ * (1.0 - alpha) + az * alpha;

  float linX = ax - gravityX;
  float linY = ay - gravityY;
  float linZ = az - gravityZ;
  float linMag = sqrt(linX * linX + linY * linY + linZ * linZ);

  float correctedMag = linMag - baselineNoise;
  if (correctedMag < 0) correctedMag = 0;

  // Update rolling peak window
  peakWindow[peakIndex] = correctedMag;
  peakIndex = (peakIndex + 1) % WINDOW_SIZE;

  // Find peak in window
  float peakMag = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) {
    if (peakWindow[i] > peakMag) peakMag = peakWindow[i];
  }

  // Calculate MMI (only if above threshold)
  int mmi = 1; // Default: no shaking
  float trigger = max(0.10f, baselineNoise * 3.0f);
  if (peakMag > trigger) {
    mmi = calculateMMI(peakMag);
  }

  // Send data every 500ms (2Hz for dashboard update)
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 500) {
    String data = String(linX, 3) + "," +
                  String(linY, 3) + "," +
                  String(linZ, 3) + "," +
                  String(peakMag, 3) + "," +
                  String(mmi) + "," +
                  String(vibRate);

    LoRa.beginPacket();
    LoRa.print(data);
    LoRa.endPacket();

    Serial.print("LinMag=");
    Serial.print(linMag, 3);
    Serial.print(" Peak=");
    Serial.print(peakMag, 3);
    Serial.print(" Noise=");
    Serial.print(baselineNoise, 3);
    Serial.print(" MMI=");
    Serial.print(mmi);
    Serial.print(" (");
    Serial.print(getMMIDescription(mmi));
    Serial.print(") Vib=");
    Serial.print(vibRate);
    Serial.print(" DO=");
    Serial.println(digitalRead(VIB_DO_PIN));

    lastSend = millis();
  }

  delay(100); // 10 Hz sampling
}

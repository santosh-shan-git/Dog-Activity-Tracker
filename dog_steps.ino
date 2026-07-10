#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

long stepCount = 0;

// Adjust after testing on your dog
const float STEP_THRESHOLD = 13.0; // m/s²
bool stepDetected = false;

void setup() {
  Serial.begin(115200);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1);
  }

  Serial.println("Dog Step Tracker Started");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  delay(1000);
}

void loop() {
  sensors_event_t accel, gyro, temp;

  mpu.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;

  // Calculate acceleration magnitude
  float magnitude = sqrt(ax * ax + ay * ay + az * az);

  // Simple peak detection
  if (magnitude > STEP_THRESHOLD && !stepDetected) {
    stepCount++;
    stepDetected = true;

    Serial.print("Steps: ");
    Serial.println(stepCount);
  }

  if (magnitude < (STEP_THRESHOLD - 2.0)) {
    stepDetected = false;
  }

  delay(20); // 50 Hz sampling
}
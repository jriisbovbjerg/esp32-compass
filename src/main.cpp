#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ICM20X.h>
#include <Adafruit_ICM20948.h>
#include <Adafruit_Sensor.h>

// Create ICM20948 object
Adafruit_ICM20948 icm;

// I2C pins for ESP32-WROOM-32
#define I2C_SDA 21
#define I2C_SCL 22

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("ESP32 ICM-20948 Compass");
  Serial.println("========================");
  
  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Initialize ICM20948
  if (!icm.begin_I2C()) {
    Serial.println("Failed to find ICM20948 chip");
    while (1) {
      delay(10);
    }
  }
  
  Serial.println("ICM20948 Found!");
  
  // Configure sensor ranges
  icm.setAccelRange(ICM20948_ACCEL_RANGE_4_G);
  icm.setGyroRange(ICM20948_GYRO_RANGE_500_DPS);
  icm.setMagDataRate(AK09916_MAG_DATARATE_10_HZ);
  
  Serial.print("Accelerometer range set to: ");
  switch (icm.getAccelRange()) {
    case ICM20948_ACCEL_RANGE_2_G:
      Serial.println("+-2G");
      break;
    case ICM20948_ACCEL_RANGE_4_G:
      Serial.println("+-4G");
      break;
    case ICM20948_ACCEL_RANGE_8_G:
      Serial.println("+-8G");
      break;
    case ICM20948_ACCEL_RANGE_16_G:
      Serial.println("+-16G");
      break;
  }
  
  Serial.print("Gyro range set to: ");
  switch (icm.getGyroRange()) {
    case ICM20948_GYRO_RANGE_250_DPS:
      Serial.println("250 degrees/s");
      break;
    case ICM20948_GYRO_RANGE_500_DPS:
      Serial.println("500 degrees/s");
      break;
    case ICM20948_GYRO_RANGE_1000_DPS:
      Serial.println("1000 degrees/s");
      break;
    case ICM20948_GYRO_RANGE_2000_DPS:
      Serial.println("2000 degrees/s");
      break;
  }
  
  Serial.println("Setup complete!");
  Serial.println();
}

void loop() {
  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t mag;
  sensors_event_t temp;
  
  // Get sensor events
  icm.getEvent(&accel, &gyro, &mag, &temp);
  
  // Calculate heading from magnetometer data
  // Heading is calculated from X and Y magnetic field components
  float heading = atan2(mag.magnetic.y, mag.magnetic.x);
  
  // Convert from radians to degrees
  heading = heading * 180.0 / PI;
  
  // Normalize to 0-360 degrees
  if (heading < 0) {
    heading += 360.0;
  }
  
  // Print sensor data
  Serial.println("----------------------------------");
  Serial.printf("Heading: %.2f°\n", heading);
  Serial.println();
  
  Serial.printf("Magnetometer (µT): X=%.2f Y=%.2f Z=%.2f\n", 
                mag.magnetic.x, mag.magnetic.y, mag.magnetic.z);
  
  Serial.printf("Accelerometer (m/s²): X=%.2f Y=%.2f Z=%.2f\n",
                accel.acceleration.x, accel.acceleration.y, accel.acceleration.z);
  
  Serial.printf("Gyroscope (rad/s): X=%.2f Y=%.2f Z=%.2f\n",
                gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);
  
  Serial.printf("Temperature: %.2f°C\n", temp.temperature);
  Serial.println();
  
  delay(500);  // Update every 500ms
}

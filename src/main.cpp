#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ICM20X.h>
#include <Adafruit_ICM20948.h>
#include <Adafruit_Sensor.h>

// Create ICM20948 object
Adafruit_ICM20948 icm;

// Magnetometer calibration values
struct MagCalibration {
  float x_min = 0;
  float x_max = 0;
  float y_min = 0;
  float y_max = 0;
  float z_min = 0;
  float z_max = 0;
  bool calibrated = false;
} magCal;

// For collecting calibration data
float mag_x_sum = 0;
float mag_y_sum = 0;
float mag_z_sum = 0;
int samples = 0;
const int SAMPLES_NEEDED = 200;  // Number of samples for calibration

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

void calibrateMagnetometer() {
  sensors_event_t accel, gyro, mag, temp;
  
  // Reset min/max values
  magCal.x_min = magCal.y_min = magCal.z_min = 99999;
  magCal.x_max = magCal.y_max = magCal.z_max = -99999;
  samples = 0;
  mag_x_sum = mag_y_sum = mag_z_sum = 0;
  
  Serial.println("\nStarting magnetometer calibration!");
  Serial.println("Please rotate the sensor in all directions (figure-8 pattern)");
  Serial.println("This will take about 10 seconds...\n");
  
  unsigned long startTime = millis();
  
  while (samples < SAMPLES_NEEDED) {
    if (icm.getEvent(&accel, &gyro, &temp, &mag)) {
      // Update min/max values
      magCal.x_min = min(magCal.x_min, mag.magnetic.x);
      magCal.x_max = max(magCal.x_max, mag.magnetic.x);
      magCal.y_min = min(magCal.y_min, mag.magnetic.y);
      magCal.y_max = max(magCal.y_max, mag.magnetic.y);
      magCal.z_min = min(magCal.z_min, mag.magnetic.z);
      magCal.z_max = max(magCal.z_max, mag.magnetic.z);
      
      // Accumulate averages
      mag_x_sum += mag.magnetic.x;
      mag_y_sum += mag.magnetic.y;
      mag_z_sum += mag.magnetic.z;
      
      samples++;
      
      if (samples % 20 == 0) {
        Serial.print("Progress: ");
        Serial.print((samples * 100) / SAMPLES_NEEDED);
        Serial.println("%");
      }
    }
    delay(50);
  }
  
  // Calculate center offsets
  float x_center = (magCal.x_min + magCal.x_max) / 2;
  float y_center = (magCal.y_min + magCal.y_max) / 2;
  float z_center = (magCal.z_min + magCal.z_max) / 2;
  
  // Calculate average field strength
  float x_avg = mag_x_sum / SAMPLES_NEEDED;
  float y_avg = mag_y_sum / SAMPLES_NEEDED;
  float z_avg = mag_z_sum / SAMPLES_NEEDED;
  
  Serial.println("\nCalibration Complete!");
  Serial.println("\nDiagnostics:");
  Serial.printf("X range: %.2f to %.2f (center: %.2f)\n", magCal.x_min, magCal.x_max, x_center);
  Serial.printf("Y range: %.2f to %.2f (center: %.2f)\n", magCal.y_min, magCal.y_max, y_center);
  Serial.printf("Z range: %.2f to %.2f (center: %.2f)\n", magCal.z_min, magCal.z_max, z_center);
  
  // Check for potential interference
  float x_spread = magCal.x_max - magCal.x_min;
  float y_spread = magCal.y_max - magCal.y_min;
  float z_spread = magCal.z_max - magCal.z_min;
  
  if (x_spread > 100 || y_spread > 100 || z_spread > 100) {
    Serial.println("\nWARNING: Large magnetic variance detected!");
    Serial.println("There may be magnetic interference nearby.");
    Serial.println("Try moving away from electronic devices, speakers, or magnetic materials.");
  }
  
  magCal.calibrated = true;
}

void loop() {
  static bool needsCalibration = true;
  
  if (needsCalibration) {
    calibrateMagnetometer();
    needsCalibration = false;
    return;
  }
  
  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t mag;
  sensors_event_t temp;
  
  // Get sensor events
  icm.getEvent(&accel, &gyro, &temp, &mag);
  
  // Apply calibration offsets
  float cal_x = mag.magnetic.x - ((magCal.x_min + magCal.x_max) / 2);
  float cal_y = mag.magnetic.y - ((magCal.y_min + magCal.y_max) / 2);
  
  // Calculate heading from calibrated data
  float heading = atan2(cal_x, cal_y);
  
  // Convert from radians to degrees and adjust to get 0° at North
  heading = (heading * 180.0 / PI) + 90.0;
  
  // Normalize to 0-360 degrees
  if (heading < 0) {
    heading += 360.0;
  }
  
  // Print sensor data
  Serial.println("----------------------------------");
  Serial.printf("Heading: %.2f°\n", heading);
  Serial.println();
  
  Serial.printf("Raw Magnetometer (µT): X=%.2f Y=%.2f Z=%.2f\n", 
                mag.magnetic.x, mag.magnetic.y, mag.magnetic.z);
  Serial.printf("Calibrated Magnetometer (µT): X=%.2f Y=%.2f\n", 
                cal_x, cal_y);
  
  Serial.printf("Accelerometer (m/s²): X=%.2f Y=%.2f Z=%.2f\n",
                accel.acceleration.x, accel.acceleration.y, accel.acceleration.z);
  
  Serial.printf("Gyroscope (rad/s): X=%.2f Y=%.2f Z=%.2f\n",
                gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);
  
  Serial.printf("Temperature: %.2f°C\n", temp.temperature);
  Serial.println();
  
  delay(500);  // Update every 500ms
}

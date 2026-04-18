#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ICM20X.h>
#include <Adafruit_ICM20948.h>
#include <Adafruit_Sensor.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>

// ======= Config =======
static const char* WIFI_SSID      = "SalkaValka";
static const char* WIFI_PASSWORD  = "97721314";
static const char* MDNS_HOSTNAME  = "navigation-input";
static const char* SIGNALK_HOST   = "geniecon-chart.local";
static const uint16_t SIGNALK_PORT = 3000;
static const char* SIGNALK_PATH   = "/signalk/v1/stream";

// ======= ICM-20948 (I2C) =======
Adafruit_ICM20948 icm;
#define I2C_SDA 21
#define I2C_SCL 22

struct MagCalibration {
  float x_min = 0, x_max = 0;
  float y_min = 0, y_max = 0;
  float z_min = 0, z_max = 0;
  bool calibrated = false;
} magCal;

int samples = 0;
const int SAMPLES_NEEDED = 200;
float lastHeadingRad = 0.0f;

// ======= NEO-7M GPS (UART2) =======
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

// ======= SignalK WebSocket =======
WebSocketsClient ws;
bool wsConnected = false;
unsigned long previousMillis = 0;
static const unsigned long PUBLISH_INTERVAL = 500;
uint32_t lastPublishMs = 0;

// ======= HTTP status server =======
WebServer server(80);

void handleStatus() {
  StaticJsonDocument<384> doc;
  doc["hostname"]     = MDNS_HOSTNAME;
  doc["ip"]           = WiFi.localIP().toString();
  doc["uptime_s"]     = millis() / 1000;
  doc["signalk"]       = wsConnected;
  doc["publish_age_s"] = lastPublishMs == 0 ? -1 : (int32_t)((millis() - lastPublishMs) / 1000);
  doc["calibrated"]    = magCal.calibrated;
  doc["heading_deg"]  = lastHeadingRad * 180.0f / PI;

  JsonObject gpsObj = doc.createNestedObject("gps");
  gpsObj["fix"]       = gps.location.isValid();
  gpsObj["sats"]      = (int)gps.satellites.value();
  if (gps.location.isValid()) {
    gpsObj["lat"]     = gps.location.lat();
    gpsObj["lng"]     = gps.location.lng();
    gpsObj["sog_kph"] = gps.speed.kmph();
    gpsObj["cog_deg"] = gps.course.deg();
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("SignalK WS connected");
      break;
    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("SignalK WS disconnected");
      break;
    default:
      break;
  }
}

void publishAll(float headingRad) {
  StaticJsonDocument<1024> doc;
  doc["context"] = "vessels.self";
  JsonArray updates = doc.createNestedArray("updates");
  JsonObject update = updates.createNestedObject();
  JsonObject source = update.createNestedObject("source");
  source["label"] = MDNS_HOSTNAME;
  JsonArray values = update.createNestedArray("values");

  // Magnetic heading (IMU)
  JsonObject vHead = values.createNestedObject();
  vHead["path"]  = "navigation.headingMagnetic";
  vHead["value"] = headingRad;

  // GPS — only when fix is fresh (< 2 s old)
  if (gps.location.isValid() && gps.location.age() < 2000) {
    JsonObject vPos = values.createNestedObject();
    vPos["path"] = "navigation.position";
    JsonObject pos = vPos.createNestedObject("value");
    pos["latitude"]  = gps.location.lat();
    pos["longitude"] = gps.location.lng();

    if (gps.speed.isValid()) {
      JsonObject vSog = values.createNestedObject();
      vSog["path"]  = "navigation.speedOverGround";
      vSog["value"] = gps.speed.mps();
    }

    // COG is meaningless when stationary — suppress below 0.5 km/h
    if (gps.course.isValid() && gps.speed.kmph() > 0.5) {
      JsonObject vCog = values.createNestedObject();
      vCog["path"]  = "navigation.courseOverGroundTrue";
      vCog["value"] = gps.course.deg() * PI / 180.0;
    }

    // Publish GPS datetime so NMEA converters get valid timestamps
    if (gps.date.isValid() && gps.time.isValid()) {
      char iso[28];
      snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
        gps.date.year(), gps.date.month(), gps.date.day(),
        gps.time.hour(), gps.time.minute(), gps.time.second());
      JsonObject vDt = values.createNestedObject();
      vDt["path"]  = "navigation.datetime";
      vDt["value"] = iso;
    }

    // Satellite count for NMEA GGA quality
    if (gps.satellites.isValid()) {
      JsonObject vSat = values.createNestedObject();
      vSat["path"]  = "navigation.gnss.satellites";
      vSat["value"] = (int)gps.satellites.value();
    }
  }

  String msg;
  serializeJson(doc, msg);
  ws.sendTXT(msg);
  lastPublishMs = millis();
}

// ======= Setup helpers =======
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WiFi: connecting to \"%s\"", WIFI_SSID);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 50) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\nWiFi failed — will retry in loop");
}

void setupOTA() {
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.onStart([]()  { Serial.println("OTA start"); });
  ArduinoOTA.onEnd([]()    { Serial.println("\nOTA end"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]\n", error);
  });
  ArduinoOTA.begin();
  Serial.printf("OTA ready: %s.local\n", MDNS_HOSTNAME);
}

void calibrateMagnetometer() {
  sensors_event_t accel, gyro, mag, temp;

  magCal.x_min = magCal.y_min = magCal.z_min =  99999;
  magCal.x_max = magCal.y_max = magCal.z_max = -99999;
  samples = 0;

  Serial.println("\nStarting magnetometer calibration!");
  Serial.println("Rotate sensor in figure-8 pattern (~10 seconds)...\n");

  while (samples < SAMPLES_NEEDED) {
    // Keep feeding GPS during calibration
    while (gpsSerial.available()) gps.encode(gpsSerial.read());

    if (icm.getEvent(&accel, &gyro, &temp, &mag)) {
      magCal.x_min = min(magCal.x_min, mag.magnetic.x);
      magCal.x_max = max(magCal.x_max, mag.magnetic.x);
      magCal.y_min = min(magCal.y_min, mag.magnetic.y);
      magCal.y_max = max(magCal.y_max, mag.magnetic.y);
      magCal.z_min = min(magCal.z_min, mag.magnetic.z);
      magCal.z_max = max(magCal.z_max, mag.magnetic.z);
      samples++;
      if (samples % 20 == 0)
        Serial.printf("Progress: %d%%\n", (samples * 100) / SAMPLES_NEEDED);
    }
    delay(50);
  }

  Serial.printf("\nCalibration complete!\n");
  Serial.printf("X: %.2f–%.2f  Y: %.2f–%.2f  Z: %.2f–%.2f\n",
    magCal.x_min, magCal.x_max, magCal.y_min, magCal.y_max, magCal.z_min, magCal.z_max);
  magCal.calibrated = true;
}

// ======= Setup =======
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("navigation-input: ICM-20948 + NEO-7M");
  Serial.println("======================================");

  // GPS UART
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.printf("GPS: UART2 RX=%d TX=%d @ %d baud\n", GPS_RX, GPS_TX, GPS_BAUD);

  // IMU I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!icm.begin_I2C()) {
    Serial.println("Failed to find ICM20948 chip");
    while (1) delay(10);
  }
  Serial.println("ICM20948 found");
  icm.setAccelRange(ICM20948_ACCEL_RANGE_4_G);
  icm.setGyroRange(ICM20948_GYRO_RANGE_500_DPS);
  icm.setMagDataRate(AK09916_MAG_DATARATE_10_HZ);

  server.on("/status", handleStatus);

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(MDNS_HOSTNAME))
      Serial.printf("mDNS: %s.local\n", MDNS_HOSTNAME);
    setupOTA();
    server.begin();
    ws.begin(SIGNALK_HOST, SIGNALK_PORT, SIGNALK_PATH);
    ws.onEvent(onWebSocketEvent);
    ws.setReconnectInterval(5000);
  }
}

// ======= Loop =======
void loop() {
  ArduinoOTA.handle();
  ws.loop();
  server.handleClient();

  // Feed GPS parser
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  static bool needsCalibration = true;
  if (needsCalibration) {
    calibrateMagnetometer();
    needsCalibration = false;
    return;
  }

  // WiFi watchdog
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastWiFiRetry = 0;
    uint32_t now = millis();
    if (now - lastWiFiRetry > 30000) {
      Serial.println("WiFi: reconnecting...");
      connectWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        MDNS.begin(MDNS_HOSTNAME);
        setupOTA();
        server.begin();
        ws.begin(SIGNALK_HOST, SIGNALK_PORT, SIGNALK_PATH);
        ws.onEvent(onWebSocketEvent);
        ws.setReconnectInterval(5000);
      }
      lastWiFiRetry = now;
    }
    delay(500);
    return;
  }

  unsigned long now = millis();
  if (now - previousMillis < PUBLISH_INTERVAL) return;
  previousMillis = now;

  sensors_event_t accel, gyro, mag, temp;
  icm.getEvent(&accel, &gyro, &temp, &mag);

  float cal_x = mag.magnetic.x - ((magCal.x_min + magCal.x_max) / 2);
  float cal_y = mag.magnetic.y - ((magCal.y_min + magCal.y_max) / 2);

  float headingDeg = (atan2(cal_x, cal_y) * 180.0f / PI) + 90.0f;
  if (headingDeg < 0)    headingDeg += 360.0f;
  if (headingDeg >= 360) headingDeg -= 360.0f;
  lastHeadingRad = headingDeg * PI / 180.0f;

  Serial.printf("Hdg: %.1f°  GPS fix:%s sats:%d",
    headingDeg,
    gps.location.isValid() ? "Y" : "N",
    (int)gps.satellites.value());
  if (gps.location.isValid())
    Serial.printf("  %.6f,%.6f  SOG:%.1fkph  COG:%.1f°",
      gps.location.lat(), gps.location.lng(),
      gps.speed.kmph(), gps.course.deg());
  Serial.println();

  if (wsConnected) publishAll(lastHeadingRad);
}

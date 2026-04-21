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
#include <Preferences.h>

// ======= Config =======
static const char* WIFI_SSID      = "SalkaValka";
static const char* WIFI_PASSWORD  = "97721314";
static const char* MDNS_HOSTNAME  = "navigation-input";
static const char* SIGNALK_HOST   = "geniecon-chart.local";
static const uint16_t SIGNALK_PORT = 3000;
static const char* SIGNALK_PATH   = "/signalk/v1/stream";

static const unsigned long PUBLISH_INTERVAL = 500;  // ms
static const uint32_t CAL_TIMEOUT_MS        = 600000; // 10 min safety timeout
static const int      CAL_MIN_SAMPLES       = 200;   // minimum before /calibrate/stop is accepted

// ======= ICM-20948 (I2C) =======
Adafruit_ICM20948 icm;
#define I2C_SDA 21
#define I2C_SCL 22

struct MagCalibration {
  float x_min = 0, x_max = 0;
  float y_min = 0, y_max = 0;
  float z_min = 0, z_max = 0;
  bool  calibrated = false;
} magCal;

// Calibration state machine
enum CalState { CAL_IDLE, CAL_RUNNING };
CalState calState   = CAL_IDLE;
uint32_t calStartMs = 0;
int      calSamples = 0;

float lastHeadingRad = 0.0f;

// NVS
Preferences prefs;

void saveCalibration() {
  prefs.begin("magcal", false);
  prefs.putFloat("x_min", magCal.x_min);
  prefs.putFloat("x_max", magCal.x_max);
  prefs.putFloat("y_min", magCal.y_min);
  prefs.putFloat("y_max", magCal.y_max);
  prefs.putFloat("z_min", magCal.z_min);
  prefs.putFloat("z_max", magCal.z_max);
  prefs.putBool ("ok",    true);
  prefs.end();
  Serial.println("Calibration saved to NVS");
}

bool loadCalibration() {
  prefs.begin("magcal", true);
  bool ok = prefs.getBool("ok", false);
  if (ok) {
    magCal.x_min = prefs.getFloat("x_min", 0);
    magCal.x_max = prefs.getFloat("x_max", 0);
    magCal.y_min = prefs.getFloat("y_min", 0);
    magCal.y_max = prefs.getFloat("y_max", 0);
    magCal.z_min = prefs.getFloat("z_min", 0);
    magCal.z_max = prefs.getFloat("z_max", 0);
    magCal.calibrated = true;
  }
  prefs.end();
  return ok;
}

// ======= NEO-7M GPS (UART2) =======
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

// ======= SignalK WebSocket =======
WebSocketsClient ws;
bool wsConnected    = false;
unsigned long previousMillis = 0;
uint32_t lastPublishMs = 0;

// ======= HTTP status server =======
WebServer server(80);

void handleStatus() {
  StaticJsonDocument<512> doc;
  doc["hostname"]      = MDNS_HOSTNAME;
  doc["ip"]            = WiFi.localIP().toString();
  doc["uptime_s"]      = millis() / 1000;
  doc["signalk"]       = wsConnected;
  doc["publish_age_s"] = lastPublishMs == 0 ? -1 : (int32_t)((millis() - lastPublishMs) / 1000);
  doc["calibrated"]    = magCal.calibrated;

  if (calState == CAL_RUNNING) {
    doc["cal_running"]  = true;
    doc["cal_elapsed_s"] = (millis() - calStartMs) / 1000;
    doc["cal_samples"]  = calSamples;
    doc["cal_ready"]    = calSamples >= CAL_MIN_SAMPLES;
  }

  if (magCal.calibrated) {
    doc["heading_deg"] = lastHeadingRad * 180.0f / PI;
  }

  JsonObject gpsObj = doc.createNestedObject("gps");
  gpsObj["fix"]  = gps.location.isValid();
  gpsObj["sats"] = (int)gps.satellites.value();
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

void handleCalibrateStart() {
  if (calState == CAL_RUNNING) {
    server.send(200, "application/json",
      "{\"status\":\"already_running\",\"samples\":" + String(calSamples) + "}");
    return;
  }

  // Reset and start
  magCal.x_min = magCal.y_min = magCal.z_min =  99999;
  magCal.x_max = magCal.y_max = magCal.z_max = -99999;
  magCal.calibrated = false;
  calSamples  = 0;
  calState    = CAL_RUNNING;
  calStartMs  = millis();

  Serial.println("Calibration started — motor in slow circles, then GET /calibrate/stop");
  server.send(200, "application/json",
    "{\"status\":\"started\","
    "\"instruction\":\"Motor boat in 2 slow full circles, then GET /calibrate/stop\","
    "\"min_samples\":" + String(CAL_MIN_SAMPLES) + "}");
}

void handleCalibrateStop() {
  if (calState != CAL_RUNNING) {
    server.send(200, "application/json", "{\"status\":\"not_running\"}");
    return;
  }
  if (calSamples < CAL_MIN_SAMPLES) {
    server.send(200, "application/json",
      "{\"status\":\"too_few_samples\",\"samples\":" + String(calSamples) +
      ",\"min\":" + String(CAL_MIN_SAMPLES) + "}");
    return;
  }
  magCal.calibrated = true;
  calState = CAL_IDLE;
  saveCalibration();
  Serial.printf("Calibration stopped by user: %d samples\n", calSamples);
  Serial.printf("  X: %.2f–%.2f  Y: %.2f–%.2f  Z: %.2f–%.2f\n",
    magCal.x_min, magCal.x_max, magCal.y_min, magCal.y_max,
    magCal.z_min, magCal.z_max);
  server.send(200, "application/json",
    "{\"status\":\"saved\",\"samples\":" + String(calSamples) + "}");
}

// ======= SignalK =======
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
  update.createNestedObject("source")["label"] = MDNS_HOSTNAME;
  JsonArray values = update.createNestedArray("values");

  JsonObject vHead = values.createNestedObject();
  vHead["path"]  = "navigation.headingMagnetic";
  vHead["value"] = headingRad;

  if (gps.location.isValid() && gps.location.age() < 2000) {
    JsonObject vPos = values.createNestedObject();
    vPos["path"] = "navigation.position";
    JsonObject pos = vPos.createNestedObject("value");
    pos["latitude"]  = gps.location.lat();
    pos["longitude"] = gps.location.lng();

    if (gps.speed.isValid()) {
      JsonObject v = values.createNestedObject();
      v["path"]  = "navigation.speedOverGround";
      v["value"] = gps.speed.mps();
    }
    if (gps.course.isValid() && gps.speed.kmph() > 0.5) {
      JsonObject v = values.createNestedObject();
      v["path"]  = "navigation.courseOverGroundTrue";
      v["value"] = gps.course.deg() * PI / 180.0;
    }
    if (gps.date.isValid() && gps.time.isValid()) {
      char iso[28];
      snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
        gps.date.year(), gps.date.month(), gps.date.day(),
        gps.time.hour(), gps.time.minute(), gps.time.second());
      JsonObject v = values.createNestedObject();
      v["path"]  = "navigation.datetime";
      v["value"] = iso;
    }
    if (gps.satellites.isValid()) {
      JsonObject v = values.createNestedObject();
      v["path"]  = "navigation.gnss.satellites";
      v["value"] = (int)gps.satellites.value();
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
    delay(500); Serial.print("."); tries++;
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
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.printf("OTA: %u%%\r", p / (t / 100));
  });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error[%u]\n", e); });
  ArduinoOTA.begin();
}

void initNetwork() {
  if (MDNS.begin(MDNS_HOSTNAME))
    Serial.printf("mDNS: %s.local\n", MDNS_HOSTNAME);
  setupOTA();
  server.begin();
  ws.begin(SIGNALK_HOST, SIGNALK_PORT, SIGNALK_PATH);
  ws.onEvent(onWebSocketEvent);
  ws.setReconnectInterval(5000);
}

// ======= Setup =======
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("navigation-input: ICM-20948 + NEO-7M");
  Serial.println("======================================");

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.printf("GPS: UART2 RX=%d TX=%d @ %d baud\n", GPS_RX, GPS_TX, GPS_BAUD);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!icm.begin_I2C()) {
    Serial.println("ICM20948 not found — halting");
    while (1) delay(10);
  }
  Serial.println("ICM20948 found");
  icm.setAccelRange(ICM20948_ACCEL_RANGE_4_G);
  icm.setGyroRange(ICM20948_GYRO_RANGE_500_DPS);
  icm.setMagDataRate(AK09916_MAG_DATARATE_10_HZ);

  if (loadCalibration()) {
    Serial.printf("Calibration loaded from NVS\n");
    Serial.printf("  X: %.2f–%.2f  Y: %.2f–%.2f  Z: %.2f–%.2f\n",
      magCal.x_min, magCal.x_max, magCal.y_min, magCal.y_max,
      magCal.z_min, magCal.z_max);
  } else {
    Serial.println("No calibration stored — heading will not be published");
    Serial.println("Trigger calibration: GET http://navigation-input.local/calibrate/start");
  }

  server.on("/status",           handleStatus);
  server.on("/calibrate/start",  handleCalibrateStart);
  server.on("/calibrate/stop",   handleCalibrateStop);

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) initNetwork();
}

// ======= Loop =======
void loop() {
  ArduinoOTA.handle();
  ws.loop();
  server.handleClient();

  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  // WiFi watchdog
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastWiFiRetry = 0;
    uint32_t now = millis();
    if (now - lastWiFiRetry > 30000) {
      Serial.println("WiFi: reconnecting...");
      connectWiFi();
      if (WiFi.status() == WL_CONNECTED) initNetwork();
      lastWiFiRetry = now;
    }
    return;
  }

  // Calibration collection (non-blocking, runs in-loop)
  if (calState == CAL_RUNNING) {
    sensors_event_t accel, gyro, mag, temp;
    if (icm.getEvent(&accel, &gyro, &temp, &mag)) {
      magCal.x_min = min(magCal.x_min, mag.magnetic.x);
      magCal.x_max = max(magCal.x_max, mag.magnetic.x);
      magCal.y_min = min(magCal.y_min, mag.magnetic.y);
      magCal.y_max = max(magCal.y_max, mag.magnetic.y);
      magCal.z_min = min(magCal.z_min, mag.magnetic.z);
      magCal.z_max = max(magCal.z_max, mag.magnetic.z);
      calSamples++;
      if (calSamples % 20 == 0)
        Serial.printf("  cal: %d samples, %lus elapsed\n",
          calSamples, (millis() - calStartMs) / 1000);
    }
    // Safety timeout: auto-save after 10 minutes if enough samples collected
    if (millis() - calStartMs >= CAL_TIMEOUT_MS) {
      if (calSamples >= CAL_MIN_SAMPLES) {
        magCal.calibrated = true;
        calState = CAL_IDLE;
        saveCalibration();
        Serial.printf("Calibration timeout auto-save: %d samples\n", calSamples);
      } else {
        calState = CAL_IDLE;
        Serial.printf("Calibration timeout: only %d samples, discarded\n", calSamples);
      }
    }
    return;  // don't publish during calibration
  }

  uint32_t now = millis();
  if (now - previousMillis < PUBLISH_INTERVAL) return;
  previousMillis = now;

  sensors_event_t accel, gyro, mag, temp;
  icm.getEvent(&accel, &gyro, &temp, &mag);

  if (!magCal.calibrated) {
    // No calibration — still feed GPS but don't publish heading
    Serial.printf("uncal | GPS fix:%s sats:%d\n",
      gps.location.isValid() ? "Y" : "N", (int)gps.satellites.value());
    return;
  }

  float cal_x = mag.magnetic.x - ((magCal.x_min + magCal.x_max) / 2.0f);
  float cal_y = mag.magnetic.y - ((magCal.y_min + magCal.y_max) / 2.0f);

  float headingDeg = (atan2(cal_x, cal_y) * 180.0f / PI) + 90.0f;
  if (headingDeg < 0)    headingDeg += 360.0f;
  if (headingDeg >= 360) headingDeg -= 360.0f;
  lastHeadingRad = headingDeg * PI / 180.0f;

  Serial.printf("Hdg: %.1f°  GPS fix:%s sats:%d",
    headingDeg, gps.location.isValid() ? "Y" : "N",
    (int)gps.satellites.value());
  if (gps.location.isValid())
    Serial.printf("  %.5f,%.5f  SOG:%.1fkph  COG:%.1f°",
      gps.location.lat(), gps.location.lng(),
      gps.speed.kmph(), gps.course.deg());
  Serial.println();

  if (wsConnected) publishAll(lastHeadingRad);
}

#include "tank_client.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>
#include "config.h"

namespace {
bool validTankConfig(const TankRuntimeConfig& c) {
  return isfinite(c.tankHeightCm) && c.tankHeightCm >= 30.0f && c.tankHeightCm <= 500.0f &&
      isfinite(c.distanceEmptyCm) && isfinite(c.distanceFullCm) &&
      isfinite(c.minDistanceCm) && isfinite(c.maxDistanceCm) &&
      c.distanceEmptyCm > c.distanceFullCm && c.minDistanceCm >= ULTRASONIC_MIN_DISTANCE_CM &&
      c.distanceFullCm >= c.minDistanceCm && c.minDistanceCm < c.maxDistanceCm &&
      c.maxDistanceCm <= 1000.0f && c.distanceEmptyCm <= c.maxDistanceCm &&
      c.sampleIntervalMs >= 500UL && c.sampleIntervalMs <= 10000UL &&
      (c.filterCount == 3 || c.filterCount == 5 || c.filterCount == 7 || c.filterCount == 9);
}

bool saveTankConfig(const TankRuntimeConfig& c, uint32_t version) {
  if (!validTankConfig(c)) return false;
  Preferences p;
  if (!p.begin("tankcfg", false)) return false;
  bool ok = p.putUChar("schema", 0) == sizeof(uint8_t) &&
      p.putFloat("height", c.tankHeightCm) == sizeof(float) &&
      p.putFloat("empty", c.distanceEmptyCm) == sizeof(float) &&
      p.putFloat("full", c.distanceFullCm) == sizeof(float) &&
      p.putFloat("min", c.minDistanceCm) == sizeof(float) &&
      p.putFloat("max", c.maxDistanceCm) == sizeof(float) &&
      p.putUInt("interval", c.sampleIntervalMs) == sizeof(uint32_t) &&
      p.putUChar("filter", c.filterCount) == sizeof(uint8_t);
  if (ok) ok = p.putUInt("version", version) == sizeof(uint32_t);
  if (ok) ok = p.putUChar("schema", 1) == sizeof(uint8_t);
  p.end();
  return ok;
}
}

bool TankClient::loadConfig(TankRuntimeConfig& c) {
  c.tankHeightCm = TANK_HEIGHT_CM;
  c.distanceEmptyCm = DISTANCE_AT_EMPTY_CM;
  c.distanceFullCm = DISTANCE_AT_FULL_CM;
  c.minDistanceCm = ULTRASONIC_MIN_DISTANCE_CM;
  c.maxDistanceCm = ULTRASONIC_MAX_DISTANCE_CM;
  c.sampleIntervalMs = SENSOR_READ_INTERVAL_MS;
  c.filterCount = LEVEL_SAMPLE_COUNT;
  Preferences p;
  if (!p.begin("tankcfg", true)) return false;
  const bool present = p.getUChar("schema", 0) == 1;
  if (present) {
    c.tankHeightCm = p.getFloat("height", c.tankHeightCm);
    c.distanceEmptyCm = p.getFloat("empty", c.distanceEmptyCm);
    c.distanceFullCm = p.getFloat("full", c.distanceFullCm);
    c.minDistanceCm = p.getFloat("min", c.minDistanceCm);
    c.maxDistanceCm = p.getFloat("max", c.maxDistanceCm);
    c.sampleIntervalMs = p.getUInt("interval", c.sampleIntervalMs);
    c.filterCount = p.getUChar("filter", c.filterCount);
    configVersion_ = p.getUInt("version", 0);
  }
  p.end();
  if (!present || !validTankConfig(c)) {
    configVersion_ = 0;
    c.tankHeightCm = TANK_HEIGHT_CM;
    c.distanceEmptyCm = DISTANCE_AT_EMPTY_CM;
    c.distanceFullCm = DISTANCE_AT_FULL_CM;
    c.minDistanceCm = ULTRASONIC_MIN_DISTANCE_CM;
    c.maxDistanceCm = ULTRASONIC_MAX_DISTANCE_CM;
    c.sampleIntervalMs = SENSOR_READ_INTERVAL_MS;
    c.filterCount = LEVEL_SAMPLE_COUNT;
    return false;
  }
  return true;
}

void TankClient::begin() {
  bootId_ = esp_random();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttemptAt_ = millis();
}

bool TankClient::isConnected() const { return WiFi.status() == WL_CONNECTED; }

void TankClient::updateWiFi() {
  if (isConnected() || millis() - lastWiFiAttemptAt_ < WIFI_RETRY_INTERVAL_MS) return;
  lastWiFiAttemptAt_ = millis();
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Wi-Fi reconnect attempt");
}

int TankClient::postReading(const TankReading& reading, TankRuntimeConfig& config) {
  if (!isConnected()) return -1;

  WiFiClient client;
  HTTPClient http;
  const String url = String("http://") + PUMP_CONTROLLER_IP + ":" +
                     String(PUMP_CONTROLLER_PORT) + TANK_POST_PATH;
  if (!http.begin(client, url)) return -2;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  JsonDocument payload;
  payload["level_percent"] = reading.levelPercent;
  payload["float_full"] = reading.floatFull;
  payload["sensor_valid"] = reading.ultrasonicValid;
  payload["distance_cm"] = reading.ultrasonicValid ? reading.distanceCm : -1.0f;
  payload["boot_id"] = bootId_;
  payload["seq"] = reading.sequence;
  payload["firmware"] = "2.4.0";
  payload["config_version"] = configVersion_;
  String body;
  serializeJson(payload, body);

  const int status = http.POST(body);
  if (status == 200) {
    JsonDocument response;
    if (!deserializeJson(response, http.getString())) {
      JsonObjectConst c = response["tank_config"].as<JsonObjectConst>();
      if (!c.isNull()) {
        const char* floatKeys[] = {"tank_height_cm", "distance_empty_cm", "distance_full_cm", "sensor_min_distance_cm", "sensor_max_distance_cm"};
        bool fieldsValid = c["config_version"].is<uint32_t>() &&
            (c["sensor_interval_ms"].is<uint32_t>() || c["sensor_interval_ms"].is<int>()) &&
            (c["filter_count"].is<uint8_t>() || c["filter_count"].is<int>());
        for (const char* key : floatKeys)
          fieldsValid = fieldsValid && (c[key].is<float>() || c[key].is<int>());
        if (!fieldsValid) {
          Serial.println("[CONFIG][ERROR] rejected incomplete or mistyped Tank configuration");
          http.end();
          return status;
        }
        const float emptyCm = c["distance_empty_cm"].as<float>();
        const float fullCm = c["distance_full_cm"].as<float>();
        const float minCm = c["sensor_min_distance_cm"].as<float>();
        const float maxCm = c["sensor_max_distance_cm"].as<float>();
        const float heightCm = c["tank_height_cm"].as<float>();
        const uint8_t filter = c["filter_count"].as<uint8_t>();
        TankRuntimeConfig candidate = config;
        candidate.tankHeightCm = heightCm;
        candidate.distanceEmptyCm = emptyCm; candidate.distanceFullCm = fullCm;
        candidate.minDistanceCm = minCm; candidate.maxDistanceCm = maxCm;
        candidate.sampleIntervalMs = c["sensor_interval_ms"].as<uint32_t>();
        candidate.filterCount = filter;
        const uint32_t version = c["config_version"] | 0UL;
        if (version != 0 && version != configVersion_ && saveTankConfig(candidate, version)) {
          config = candidate;
          configVersion_ = version;
          Serial.printf("[CONFIG] Tank configuration persisted to NVS (version %lu)\n", static_cast<unsigned long>(version));
        } else if (version != 0 && version != configVersion_) {
          Serial.println("[CONFIG][ERROR] Tank NVS write failed; retaining previous configuration");
        }
      }
    }
  }
  if (status < 0) Serial.printf("POST failed: %s\n", http.errorToString(status).c_str());
  http.end();
  return status;
}

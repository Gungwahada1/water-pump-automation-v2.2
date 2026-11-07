#include "tank_client.h"
#include <ArduinoJson.h>
#include "config.h"

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
  payload["firmware"] = "2.3.0";
  String body;
  serializeJson(payload, body);

  const int status = http.POST(body);
  if (status == 200) {
    JsonDocument response;
    if (!deserializeJson(response, http.getString())) {
      JsonObjectConst c = response["tank_config"].as<JsonObjectConst>();
      if (!c.isNull()) {
        const float emptyCm = c["distance_empty_cm"] | config.distanceEmptyCm;
        const float fullCm = c["distance_full_cm"] | config.distanceFullCm;
        const float minCm = c["sensor_min_distance_cm"] | config.minDistanceCm;
        const float maxCm = c["sensor_max_distance_cm"] | config.maxDistanceCm;
        const uint8_t filter = c["filter_count"] | config.filterCount;
        if (emptyCm > fullCm && fullCm >= minCm && minCm < maxCm && maxCm <= 1000.0f &&
            filter >= 3 && filter <= 9 && filter % 2) {
          config.tankHeightCm = c["tank_height_cm"] | config.tankHeightCm;
          config.distanceEmptyCm = emptyCm; config.distanceFullCm = fullCm;
          config.minDistanceCm = minCm; config.maxDistanceCm = maxCm;
          config.sampleIntervalMs = c["sensor_interval_ms"] | config.sampleIntervalMs;
          config.filterCount = filter;
        }
      }
    }
  }
  if (status < 0) Serial.printf("POST failed: %s\n", http.errorToString(status).c_str());
  http.end();
  return status;
}

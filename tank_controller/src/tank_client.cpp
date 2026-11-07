#include "tank_client.h"
#include <ArduinoJson.h>
#include "config.h"

void TankClient::begin() {
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

int TankClient::postReading(const TankReading& reading) {
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
  String body;
  serializeJson(payload, body);

  const int status = http.POST(body);
  if (status < 0) Serial.printf("POST failed: %s\n", http.errorToString(status).c_str());
  http.end();
  return status;
}

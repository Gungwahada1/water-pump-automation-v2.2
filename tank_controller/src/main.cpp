#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "tank_client.h"
#include "tank_sensors.h"

namespace {
TankSensors tankSensors;
TankClient tankClient;
TankReading latestReading;
uint32_t lastSensorReadAt = 0;
uint32_t lastPostAt = 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  tankSensors.begin();
  tankClient.begin();
  Serial.println("Tank Controller starting as Wi-Fi client");
}

void loop() {
  const uint32_t now = millis();
  tankClient.updateWiFi();

  if (now - lastSensorReadAt >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadAt = now;
    latestReading = tankSensors.read();
    digitalWrite(PIN_STATUS_LED, tankClient.isConnected() ? HIGH : LOW);
    Serial.printf("Wi-Fi=%s distance=%.1f cm level=%.1f%% ultrasonic=%s float_full=%s\n",
                  tankClient.isConnected() ? "connected" : "disconnected",
                  latestReading.distanceCm, latestReading.levelPercent,
                  latestReading.ultrasonicValid ? "valid" : "invalid",
                  latestReading.floatFull ? "true" : "false");
  }

  if (now - lastPostAt >= HTTP_POST_INTERVAL_MS) {
    lastPostAt = now;
    const int status = tankClient.postReading(latestReading);
    if (status > 0) Serial.printf("Tank data HTTP status: %d\n", status);
  }
  delay(5);
}

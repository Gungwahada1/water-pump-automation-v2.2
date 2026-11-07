#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "tank_client.h"
#include "tank_sensors.h"

namespace {
TankSensors tankSensors;
TankClient tankClient;
TankReading latestReading;
TankRuntimeConfig tankConfig;
uint32_t lastSensorReadAt = 0;
uint32_t lastPostAt = 0;
uint32_t sampleSequence = 0;
bool newReadingPending = false;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  tankSensors.begin();
  tankConfig.tankHeightCm = TANK_HEIGHT_CM;
  tankConfig.distanceEmptyCm = DISTANCE_AT_EMPTY_CM;
  tankConfig.distanceFullCm = DISTANCE_AT_FULL_CM;
  tankConfig.minDistanceCm = ULTRASONIC_MIN_DISTANCE_CM;
  tankConfig.maxDistanceCm = ULTRASONIC_MAX_DISTANCE_CM;
  tankConfig.sampleIntervalMs = SENSOR_READ_INTERVAL_MS;
  tankConfig.filterCount = LEVEL_SAMPLE_COUNT;
  tankClient.begin();
  Serial.println("Tank Controller starting as Wi-Fi client");
}

void loop() {
  const uint32_t now = millis();
  tankClient.updateWiFi();

  if (now - lastSensorReadAt >= tankConfig.sampleIntervalMs) {
    lastSensorReadAt = now;
    latestReading = tankSensors.read(tankConfig);
    latestReading.sequence = ++sampleSequence;
    newReadingPending = true;
    digitalWrite(PIN_STATUS_LED, tankClient.isConnected() ? HIGH : LOW);
    Serial.printf("Wi-Fi=%s distance=%.1f cm level=%.1f%% ultrasonic=%s float_full=%s\n",
                  tankClient.isConnected() ? "connected" : "disconnected",
                  latestReading.distanceCm, latestReading.levelPercent,
                  latestReading.ultrasonicValid ? "valid" : "invalid",
                  latestReading.floatFull ? "true" : "false");
  }

  if (newReadingPending && now - lastPostAt >= HTTP_POST_INTERVAL_MS) {
    lastPostAt = now;
    const int status = tankClient.postReading(latestReading, tankConfig);
    if (status == 200) newReadingPending = false;
    if (status > 0) Serial.printf("Tank data HTTP status: %d\n", status);
  }
  delay(5);
}

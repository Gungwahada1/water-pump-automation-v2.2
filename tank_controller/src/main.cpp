#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "tank_client.h"
#include "tank_sensors.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
TankSensors tankSensors;
TankClient tankClient;
TankReading latestReading;
TankRuntimeConfig tankConfig;
uint32_t lastSensorReadAt = 0;
uint32_t lastPostAt = 0;
uint32_t sampleSequence = 0;
bool newReadingPending = false;
volatile bool resetTaskRunning = false;
bool resetStableHigh = true;
bool resetLastRawHigh = true;
uint32_t resetChangedAt = 0;

void restartTankController() {
  Serial.println("[RESET] external reset button pressed; restarting Tank Controller");
  digitalWrite(PIN_STATUS_LED, LOW);
  delay(20);
  ESP.restart();
}

bool resetButtonWasPressed() {
  const bool rawHigh = digitalRead(PIN_BUTTON_RESET) == HIGH;
  if (rawHigh != resetLastRawHigh) {
    resetLastRawHigh = rawHigh;
    resetChangedAt = millis();
  }
  if (rawHigh != resetStableHigh && millis() - resetChangedAt >= RESET_BUTTON_DEBOUNCE_MS) {
    resetStableHigh = rawHigh;
    return !resetStableHigh;
  }
  return false;
}

void resetButtonTask(void*) {
  for (;;) {
    if (resetButtonWasPressed()) restartTankController();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON_RESET, INPUT_PULLUP);
  resetStableHigh = resetLastRawHigh = digitalRead(PIN_BUTTON_RESET) == HIGH;
  resetChangedAt = millis();
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  tankSensors.begin();
  const bool tankConfigLoaded = tankClient.loadConfig(tankConfig);
  Serial.printf("[CONFIG] Tank settings %s NVS; interval=%lu ms filter=%u\n",
                tankConfigLoaded ? "loaded from" : "using safe defaults (NVS empty/invalid)",
                static_cast<unsigned long>(tankConfig.sampleIntervalMs),
                static_cast<unsigned>(tankConfig.filterCount));
  tankClient.begin();
  resetTaskRunning = true;
  if (xTaskCreatePinnedToCore(resetButtonTask, "tank_reset_button", 3072, nullptr, 2, nullptr, 1) != pdPASS) {
    resetTaskRunning = false;
    Serial.println("[RESET][WARN] reset task unavailable; using main-loop polling");
  }
  Serial.println("Tank Controller starting as Wi-Fi client");
}

void loop() {
  const uint32_t now = millis();
  if (!resetTaskRunning && resetButtonWasPressed()) restartTankController();
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

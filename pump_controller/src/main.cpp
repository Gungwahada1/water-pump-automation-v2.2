#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "http_api.h"
#include "input_manager.h"
#include "pump_controller.h"
#include "status_display.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
PumpController pumpController;
InputManager inputManager;
HttpApi httpApi;
StatusDisplay statusDisplay;
QueueHandle_t tankSamples = nullptr;
QueueHandle_t statusSnapshots = nullptr;
uint32_t lastDebugStatusAt = 0;
}

void setup() {
  // Initialize the safety-critical output before networking or other peripherals.
  pumpController.begin();
  inputManager.begin();

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Water Pump Automation V2.2");
  Serial.println(" ESP32 Pump Controller - booting");
  Serial.println("========================================");
  Serial.printf("[BOOT] SSR output initialized OFF on GPIO%d (OFF level %s)\n",
                PIN_SSR, SSR_OFF_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.printf("[BOOT] start threshold=%.1f%%, full cutoff=%.1f%%, sensor timeout=%lu ms, max runtime=%lu ms\n",
                AUTO_START_LEVEL_PERCENT,
                ULTRASONIC_FULL_STOP_PERCENT,
                static_cast<unsigned long>(SENSOR_TIMEOUT_MS),
                static_cast<unsigned long>(MAX_PUMP_RUNTIME_MS));
  Serial.printf("[BOOT] AP SSID=%s, channel=%u\n", AP_SSID, AP_CHANNEL);

  WiFi.mode(WIFI_AP);
  const bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);
  if (apStarted) {
    Serial.printf("[WIFI] SoftAP READY: SSID=%s IP=%s max_clients=%u\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(AP_MAX_CLIENTS));
  } else {
    Serial.println("[WIFI][ERROR] SoftAP failed to start; reset the board and inspect power/firmware");
  }

  tankSamples = xQueueCreate(1, sizeof(TankSample));
  statusSnapshots = xQueueCreate(1, sizeof(PumpStatus));
  if (tankSamples == nullptr || statusSnapshots == nullptr) {
    Serial.println("[FATAL] could not allocate HTTP queues; SSR stays OFF");
    while (true) { digitalWrite(PIN_SSR, SSR_OFF_LEVEL); delay(1000); }
  }
  PumpStatus initialStatus = pumpController.statusSnapshot();
  xQueueOverwrite(statusSnapshots, &initialStatus);

  if (!httpApi.begin(tankSamples, statusSnapshots)) {
    Serial.println("[FATAL] could not start HTTP task; SSR stays OFF");
    while (true) { digitalWrite(PIN_SSR, SSR_OFF_LEVEL); delay(1000); }
  }
  if (!statusDisplay.begin(statusSnapshots))
    Serial.println("[LCD][ERROR] could not start display task; pump control continues");
  Serial.println("[READY] HTTP server starting on port 80 (separate task)");
  Serial.println("[READY] Connect Tank Controller/client to the AP, then POST /api/v1/tank");
  Serial.println("[READY] AUTO remains OFF until a valid, fresh tank sample reports level <= threshold");
}

void loop() {
  // Keep physical controls in this fast loop. HTTP parsing runs on a separate
  // FreeRTOS task, so a slow client cannot hold up button handling.
  TankSample incomingSample;
  if (tankSamples != nullptr && xQueueReceive(tankSamples, &incomingSample, 0) == pdPASS)
    pumpController.setTankSample(incomingSample);

  pumpController.update(inputManager.read());
  if (statusSnapshots != nullptr) {
    PumpStatus snapshot = pumpController.statusSnapshot();
    xQueueOverwrite(statusSnapshots, &snapshot);
  }

  if (millis() - lastDebugStatusAt >= DEBUG_STATUS_INTERVAL_MS) {
    lastDebugStatusAt = millis();
    const TankSample& sample = pumpController.tankSample();
    Serial.printf("[STATUS] mode=%s pump=%s ap_clients=%u tank_received=%s tank_valid=%s fresh=%s",
                  modeName(pumpController.mode()), pumpController.isPumpOn() ? "ON" : "OFF",
                  static_cast<unsigned>(WiFi.softAPgetStationNum()), sample.received ? "yes" : "no",
                  sample.valid ? "yes" : "no", pumpController.isSampleFresh() ? "yes" : "no");
    if (sample.received) {
      Serial.printf(" age=%lu ms level=%.1f%% float_full=%s",
                    static_cast<unsigned long>(pumpController.sampleAgeMs()), sample.levelPercent,
                    sample.floatFull ? "true" : "false");
    }
    Serial.printf(" lockout=%s stop_reason=%s\n",
                  pumpController.isRuntimeLockedOut() ? "yes" : "no",
                  stopReasonName(pumpController.stopReason()));
  }
  delay(2);
}

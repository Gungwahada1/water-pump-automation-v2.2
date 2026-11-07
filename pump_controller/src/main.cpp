#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "http_api.h"
#include "input_manager.h"
#include "pump_controller.h"
#include "status_display.h"
#include "system_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

namespace {
PumpController pumpController;
InputManager inputManager;
HttpApi httpApi;
StatusDisplay statusDisplay;
QueueHandle_t tankSamples = nullptr;
QueueHandle_t statusSnapshots = nullptr;
QueueHandle_t dashboardCommands = nullptr;
QueueHandle_t controlResults = nullptr;
QueueHandle_t runtimeSettings = nullptr;
uint32_t lastDebugStatusAt = 0;
bool lastTankFresh = false;
volatile bool softApReady = false;
bool loopWatchdogReady = false;

void wifiRecoveryTask(void*) {
  for (;;) {
    if (!softApReady || WiFi.getMode() != WIFI_AP || WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
      WiFi.mode(WIFI_AP);
      softApReady = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);
      if (softApReady) systemLog().add("Pump Access Point recovered");
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
}

void setup() {
  // Initialize the safety-critical output before networking or other peripherals.
  pumpController.begin();
  inputManager.begin();

  Serial.begin(115200);
  const esp_err_t wdtInit = esp_task_wdt_init(5, true);
  if (wdtInit != ESP_OK && wdtInit != ESP_ERR_INVALID_STATE)
    Serial.printf("[WDT][WARN] init failed: %d\n", static_cast<int>(wdtInit));
  loopWatchdogReady = esp_task_wdt_add(nullptr) == ESP_OK;
  delay(200);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Water Pump Automation V2.3");
  Serial.println(" ESP32 Pump Controller - booting");
  Serial.println("========================================");
  Serial.printf("[BOOT] SSR output initialized OFF on GPIO%d (OFF level %s)\n",
                PIN_SSR, SSR_OFF_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.printf("[BOOT] default AUTO start=%.1f%%, AUTO/MANUAL stop=%.1f%%, ultrasonic full=%.1f%%, sensor timeout=%lu ms, max runtime=%lu ms\n",
                AUTO_START_LEVEL_PERCENT,
                AUTO_STOP_LEVEL_DEFAULT_PERCENT,
                ULTRASONIC_LEVEL_FULL_PERCENT,
                static_cast<unsigned long>(SENSOR_TIMEOUT_MS),
                static_cast<unsigned long>(MAX_PUMP_RUNTIME_MS));
  Serial.printf("[BOOT] AP SSID=%s, channel=%u\n", AP_SSID, AP_CHANNEL);

  WiFi.mode(WIFI_AP);
  softApReady = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);
  if (softApReady) {
    systemLog().add("Pump Access Point started");
    Serial.printf("[WIFI] SoftAP READY: SSID=%s IP=%s max_clients=%u\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(AP_MAX_CLIENTS));
  } else {
    Serial.println("[WIFI][ERROR] SoftAP failed to start; reset the board and inspect power/firmware");
  }
  if (xTaskCreatePinnedToCore(wifiRecoveryTask, "pump_wifi_recovery", 4096, nullptr, 1, nullptr, 0) != pdPASS)
    Serial.println("[WIFI][WARN] AP recovery task unavailable; existing AP remains active");
  Serial.printf("[BOOT] reset_reason=%d loop_watchdog=%s\n", static_cast<int>(esp_reset_reason()), loopWatchdogReady ? "enabled" : "unavailable");

  tankSamples = xQueueCreate(1, sizeof(TankSample));
  statusSnapshots = xQueueCreate(1, sizeof(PumpStatus));
  dashboardCommands = xQueueCreate(4, sizeof(DashboardCommand));
  controlResults = xQueueCreate(1, sizeof(ControlResult));
  runtimeSettings = xQueueCreate(1, sizeof(SystemSettings));
  SystemSettings defaultSettings;
  defaultSettings.autoStartPercent = AUTO_START_LEVEL_PERCENT;
  defaultSettings.autoStopPercent = AUTO_STOP_LEVEL_DEFAULT_PERCENT;
  defaultSettings.maxPumpRuntimeMs = MAX_PUMP_RUNTIME_MS;
  defaultSettings.tankTimeoutMs = SENSOR_TIMEOUT_MS;
  if (runtimeSettings != nullptr) xQueueOverwrite(runtimeSettings, &defaultSettings);
  if (tankSamples == nullptr || statusSnapshots == nullptr || dashboardCommands == nullptr || controlResults == nullptr || runtimeSettings == nullptr) {
    Serial.println("[FATAL] could not allocate HTTP queues; SSR stays OFF");
    while (true) { digitalWrite(PIN_SSR, SSR_OFF_LEVEL); delay(1000); }
  }
  PumpStatus initialStatus = pumpController.statusSnapshot();
  xQueueOverwrite(statusSnapshots, &initialStatus);

  if (!httpApi.begin(tankSamples, statusSnapshots, dashboardCommands, controlResults, runtimeSettings)) {
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
  SystemSettings settings;
  if (runtimeSettings != nullptr && xQueuePeek(runtimeSettings, &settings, 0) == pdPASS)
    pumpController.setSettings(settings);

  PhysicalInputs inputs = inputManager.read();
  DashboardCommand command;
  bool dashboardOff = false;
  bool dashboardOn = false;
  bool restartRequested = false;
  uint32_t controlRequestId = 0;
  while (dashboardCommands != nullptr && xQueueReceive(dashboardCommands, &command, 0) == pdPASS) {
    if (command.kind == DashboardCommandKind::OFF) dashboardOff = true;
    else if (command.kind == DashboardCommandKind::ON) dashboardOn = true;
    else restartRequested = true;
    if (command.requestId != 0) controlRequestId = command.requestId;
  }
  inputs.dashboardOffRequested = dashboardOff;
  inputs.dashboardOnRequested = !dashboardOff && dashboardOn;
  if (restartRequested) {
    pumpController.forceOffForRestart();
    systemLog().add("SSR forced OFF; restarting pump controller");
    delay(50);
    ESP.restart();
  }
  pumpController.update(inputs);
  if (controlRequestId != 0) {
    ControlResult result;
    result.requestId = controlRequestId;
    if (dashboardOff) result.accepted = true;
    else {
      const char* reason = pumpController.requestDashboardOnReason();
      result.accepted = dashboardOn && reason == nullptr && pumpController.isPumpOn();
      if (!result.accepted) {
        const char* message = reason ? reason : "manual_off_or_safety_stop";
        strncpy(result.reason, message, sizeof(result.reason) - 1);
      }
    }
    xQueueOverwrite(controlResults, &result);
  }
  const bool tankFreshNow = pumpController.isSampleFresh();
  if (tankFreshNow != lastTankFresh) {
    systemLog().add(tankFreshNow ? "Tank Controller data is fresh" : "Tank Controller disconnected or stale");
    lastTankFresh = tankFreshNow;
  }
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
  if (loopWatchdogReady) esp_task_wdt_reset();
}

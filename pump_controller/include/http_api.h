#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>
#include "pump_types.h"
#include "config_store.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class HttpApi {
 public:
  bool begin(QueueHandle_t tankSamples, QueueHandle_t statusSnapshots, QueueHandle_t commands, QueueHandle_t results, QueueHandle_t settings);

 private:
  static void taskEntry(void* argument);
  void runServerTask();
  void handleTankPost();
  void handleStatus();
  void handleControl();
  void handleDashboard();
  void handleDevice();
  void handleLogs();
  void handleRestart();
  void handleConfig();
  void handleNotFound();
  void sendJson(int statusCode, JsonDocument& document);
  uint32_t settingsVersion(const SystemSettings& settings) const;
  void addTankConfig(JsonDocument& response, const SystemSettings& settings, uint32_t version);

  QueueHandle_t tankSamples_ = nullptr;
  QueueHandle_t statusSnapshots_ = nullptr;
  QueueHandle_t commands_ = nullptr;
  QueueHandle_t results_ = nullptr;
  QueueHandle_t settingsQueue_ = nullptr;
  WebServer server_;
  uint32_t lastTankBootId_ = 0;
  uint32_t lastTankSequence_ = 0;
  bool haveTankSequence_ = false;
  uint32_t nextControlRequestId_ = 1;
  SystemSettings pendingSettings_{};
  uint32_t pendingSettingsVersion_ = 0;
  uint32_t lastTankConfigVersion_ = 0;
  bool configSavePending_ = false;
  bool configSaveFailed_ = false;
  bool pumpConfigSaved_ = false;
  uint32_t configPendingSince_ = 0;
};

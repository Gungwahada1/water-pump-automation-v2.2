#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>
#include "pump_types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class HttpApi {
 public:
  bool begin(QueueHandle_t tankSamples, QueueHandle_t statusSnapshots);

 private:
  static void taskEntry(void* argument);
  void runServerTask();
  void handleTankPost();
  void handleStatus();
  void handleNotFound();
  void sendJson(int statusCode, JsonDocument& document);

  QueueHandle_t tankSamples_ = nullptr;
  QueueHandle_t statusSnapshots_ = nullptr;
  WebServer server_;
};

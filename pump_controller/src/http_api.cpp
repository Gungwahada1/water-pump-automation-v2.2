#include "http_api.h"
#include <ArduinoJson.h>
#include <math.h>
#include "config.h"
#include "pump_types.h"

bool HttpApi::begin(QueueHandle_t tankSamples, QueueHandle_t statusSnapshots) {
  tankSamples_ = tankSamples;
  statusSnapshots_ = statusSnapshots;
  if (tankSamples_ == nullptr || statusSnapshots_ == nullptr) return false;

  const char* headers[] = {"Content-Type"};
  server_.collectHeaders(headers, 1);
  server_.on("/api/v1/tank", HTTP_POST, [this]() { handleTankPost(); });
  server_.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
  server_.onNotFound([this]() { handleNotFound(); });
  const BaseType_t started = xTaskCreatePinnedToCore(
      taskEntry, "pump_http", 8192, this, 1, nullptr, 0);
  return started == pdPASS;
}

void HttpApi::taskEntry(void* argument) {
  static_cast<HttpApi*>(argument)->runServerTask();
}

void HttpApi::runServerTask() {
  server_.begin();
  Serial.println("[HTTP] server task ready: POST /api/v1/tank, GET /api/v1/status");
  for (;;) {
    server_.handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void HttpApi::sendJson(int statusCode, JsonDocument& document) {
  String body;
  serializeJson(document, body);
  server_.send(statusCode, "application/json", body);
}

void HttpApi::handleTankPost() {
  if (!server_.hasHeader("Content-Type") ||
      !server_.header("Content-Type").startsWith("application/json")) {
    Serial.println("[HTTP] rejected tank update: Content-Type must be application/json");
    JsonDocument response;
    response["error"] = "content_type_must_be_application_json";
    sendJson(415, response);
    return;
  }

  JsonDocument request;
  const DeserializationError error = deserializeJson(request, server_.arg("plain"));
  const bool hasNumericLevel = request["level_percent"].is<float>() ||
                               request["level_percent"].is<int>();
  if (error || !hasNumericLevel || !request["float_full"].is<bool>() ||
      !request["sensor_valid"].is<bool>()) {
    Serial.printf("[HTTP] rejected tank update: invalid JSON/payload (%s)\n",
                  error ? error.c_str() : "required fields or types missing");
    JsonDocument response;
    response["error"] = "invalid_payload";
    response["required"] = "level_percent (number), float_full (boolean), sensor_valid (boolean)";
    sendJson(400, response);
    return;
  }

  const float level = request["level_percent"].as<float>();
  if (!isfinite(level) || level < 0.0f || level > 100.0f) {
    Serial.printf("[HTTP] rejected tank update: level_percent out of range (%.2f)\n", level);
    JsonDocument response;
    response["error"] = "level_percent_out_of_range";
    sendJson(400, response);
    return;
  }

  TankSample sample;
  sample.received = true;
  sample.levelPercent = level;
  sample.floatFull = request["float_full"].as<bool>();
  sample.valid = request["sensor_valid"].as<bool>();
  sample.receivedAt = millis();
  if (xQueueOverwrite(tankSamples_, &sample) != pdPASS) {
    Serial.println("[HTTP][ERROR] could not queue tank sample");
    JsonDocument response;
    response["error"] = "sample_queue_unavailable";
    sendJson(503, response);
    return;
  }
  Serial.printf("[HTTP] tank data accepted: level=%.1f%% valid=%s float_full=%s\n",
                sample.levelPercent, sample.valid ? "true" : "false",
                sample.floatFull ? "true" : "false");

  JsonDocument response;
  response["accepted"] = true;
  response["sensor_valid"] = sample.valid;
  response["float_full"] = sample.floatFull;
  response["level_percent"] = sample.levelPercent;
  sendJson(200, response);
}

void HttpApi::handleStatus() {
  PumpStatus snapshot;
  const bool hasSnapshot = xQueuePeek(statusSnapshots_, &snapshot, 0) == pdPASS;
  JsonDocument response;
  const TankSample& sample = snapshot.tank;
  response["mode"] = modeName(snapshot.mode);
  response["pump_on"] = snapshot.pumpOn;
  response["level_percent"] = sample.levelPercent;
  response["sensor_received"] = sample.received;
  response["sensor_valid"] = sample.valid;
  response["sensor_fresh"] = snapshot.sampleFresh;
  response["sensor_age_ms"] = snapshot.sampleAgeMs;
  response["float_full"] = sample.floatFull;
  response["ultrasonic_full"] = sample.received && sample.valid &&
      sample.levelPercent >= ULTRASONIC_FULL_STOP_PERCENT;
  response["runtime_lockout"] = snapshot.runtimeLockout;
  response["stop_reason"] = stopReasonName(snapshot.stopReason);
  response["status_ready"] = hasSnapshot;
  sendJson(200, response);
}

void HttpApi::handleNotFound() {
  Serial.printf("[HTTP] 404: %s %s\n", server_.method() == HTTP_GET ? "GET" : "request",
                server_.uri().c_str());
  JsonDocument response;
  response["error"] = "not_found";
  sendJson(404, response);
}

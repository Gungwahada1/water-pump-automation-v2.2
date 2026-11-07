#include "http_api.h"
#include <ArduinoJson.h>
#include <math.h>
#include "config.h"
#include "pump_types.h"
#include "system_log.h"
#include "web_dashboard.h"
#include <string.h>

bool HttpApi::begin(QueueHandle_t tankSamples, QueueHandle_t statusSnapshots, QueueHandle_t commands, QueueHandle_t results, QueueHandle_t settings) {
  tankSamples_ = tankSamples;
  statusSnapshots_ = statusSnapshots;
  commands_ = commands;
  results_ = results;
  settingsQueue_ = settings;
  if (tankSamples_ == nullptr || statusSnapshots_ == nullptr || commands_ == nullptr || results_ == nullptr || settingsQueue_ == nullptr) return false;

  const char* headers[] = {"Content-Type"};
  server_.collectHeaders(headers, 1);
  server_.on("/api/v1/tank", HTTP_POST, [this]() { handleTankPost(); });
  server_.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/v1/control", HTTP_POST, [this]() { handleControl(); });
  server_.on("/api/v1/device", HTTP_GET, [this]() { handleDevice(); });
  server_.on("/api/v1/logs", HTTP_GET, [this]() { handleLogs(); });
  server_.on("/api/v1/config", HTTP_GET, [this]() { handleConfig(); });
  server_.on("/api/v1/config", HTTP_POST, [this]() { handleConfig(); });
  server_.on("/api/v1/device/restart", HTTP_POST, [this]() { handleRestart(); });
  server_.on("/", HTTP_GET, [this]() { handleDashboard(); });
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
  Serial.println("[HTTP] server ready: dashboard, /api/v1/status, /api/v1/control, /api/v1/tank");
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

  const bool hasBootId = request["boot_id"].is<uint32_t>();
  const bool hasSequence = request["seq"].is<uint32_t>();
  const uint32_t bootId = request["boot_id"] | 0UL;
  const uint32_t sequence = request["seq"] | 0UL;
  if (hasBootId != hasSequence) {
    JsonDocument response; response["error"] = "boot_id_and_seq_must_be_sent_together"; sendJson(400, response); return;
  }
  if (hasBootId && haveTankSequence_ && bootId == lastTankBootId_ && sequence <= lastTankSequence_) {
    JsonDocument response; response["error"] = "duplicate_or_old_sequence"; sendJson(409, response); return;
  }

  TankSample sample;
  sample.received = true;
  sample.levelPercent = level;
  if (request["distance_cm"].is<float>() || request["distance_cm"].is<int>()) {
    const float distance = request["distance_cm"].as<float>();
    if (isfinite(distance) && distance > 0.0f && distance < 1000.0f) {
      sample.distanceCm = distance;
      sample.distanceAvailable = true;
    }
  }
  sample.sequence = request["seq"] | 0U;
  if (hasBootId) { sample.bootId = bootId; sample.identityAvailable = true; }
  if (request["firmware"].is<const char*>()) {
    const char* version = request["firmware"];
    if (strlen(version) < sizeof(sample.firmware)) strncpy(sample.firmware, version, sizeof(sample.firmware) - 1);
  }
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
  if (hasBootId) { lastTankBootId_ = bootId; lastTankSequence_ = sequence; haveTankSequence_ = true; }
  Serial.printf("[HTTP] tank data accepted: level=%.1f%% valid=%s float_full=%s\n",
                sample.levelPercent, sample.valid ? "true" : "false",
                sample.floatFull ? "true" : "false");

  JsonDocument response;
  response["accepted"] = true;
  response["sensor_valid"] = sample.valid;
  response["float_full"] = sample.floatFull;
  response["level_percent"] = sample.levelPercent;
  response["distance_cm"] = sample.distanceAvailable ? sample.distanceCm : -1.0f;
  response["seq"] = sample.sequence;
  SystemSettings currentSettings;
  xQueuePeek(settingsQueue_, &currentSettings, 0);
  JsonObject tankConfig = response["tank_config"].to<JsonObject>();
  tankConfig["tank_height_cm"] = currentSettings.tankHeightCm;
  tankConfig["distance_empty_cm"] = currentSettings.distanceEmptyCm;
  tankConfig["distance_full_cm"] = currentSettings.distanceFullCm;
  tankConfig["sensor_min_distance_cm"] = currentSettings.sensorMinDistanceCm;
  tankConfig["sensor_max_distance_cm"] = currentSettings.sensorMaxDistanceCm;
  tankConfig["sensor_interval_ms"] = currentSettings.sensorIntervalMs;
  tankConfig["filter_count"] = currentSettings.filterCount;
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
  if (sample.distanceAvailable) response["distance_cm"] = sample.distanceCm;
  else response["distance_cm"] = nullptr;
  response["sensor_status"] = !sample.received ? "NO_DATA" : !sample.valid ? "INVALID" : snapshot.sampleFresh ? "OK" : "STALE";
  response["sensor_received"] = sample.received;
  response["sensor_valid"] = sample.valid;
  response["sensor_fresh"] = snapshot.sampleFresh;
  response["sensor_age_ms"] = snapshot.sampleAgeMs;
  response["float_full"] = sample.floatFull;
  response["ultrasonic_full"] = sample.received && sample.valid &&
      sample.levelPercent >= ULTRASONIC_LEVEL_FULL_PERCENT;
  response["runtime_lockout"] = snapshot.runtimeLockout;
  response["stop_reason"] = stopReasonName(snapshot.stopReason);
  response["status_ready"] = hasSnapshot;
  response["pump_runtime_ms"] = snapshot.pumpRunMs;
  SystemSettings activeSettings;
  xQueuePeek(settingsQueue_, &activeSettings, 0);
  response["max_runtime_ms"] = activeSettings.maxPumpRuntimeMs;
  response["auto_stop_percent"] = activeSettings.autoStopPercent;
  response["float_enabled"] = ENABLE_FLOAT_SWITCH_CUTOFF;
  response["uptime_ms"] = millis();
  response["free_heap"] = ESP.getFreeHeap();
  response["min_free_heap"] = ESP.getMinFreeHeap();
  response["ap_clients"] = WiFi.softAPgetStationNum();
  sendJson(200, response);
}

void HttpApi::handleControl() {
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) || !request["action"].is<const char*>()) {
    JsonDocument response; response["error"] = "invalid_payload"; sendJson(400, response); return;
  }
  const String action = request["action"].as<String>();
  DashboardCommand command;
  command.requestId = nextControlRequestId_++;
  if (action == "OFF") command.kind = DashboardCommandKind::OFF;
  else if (action == "ON") {
    PumpStatus snapshot;
    xQueuePeek(statusSnapshots_, &snapshot, 0);
    const char* reason = "status_unavailable";
    if (snapshot.mode != PumpMode::MANUAL_MODE) reason = "physical_selector_must_be_manual";
    else if (snapshot.runtimeLockout) reason = "runtime_lockout_active";
    else if (!snapshot.tank.received) reason = "tank_data_not_received";
    else if (!snapshot.sampleFresh) reason = "tank_data_stale";
    else if (!snapshot.tank.valid) reason = "ultrasonic_sensor_invalid";
    else if (snapshot.tank.levelPercent >= snapshot.autoStopLevelPercent) reason = "tank_full_ultrasonic";
    else if (ENABLE_FLOAT_SWITCH_CUTOFF && snapshot.tank.floatFull) reason = "tank_full_float";
    else reason = nullptr;
    if (reason) { JsonDocument response; response["accepted"] = false; response["reason"] = reason; sendJson(409, response); return; }
    command.kind = DashboardCommandKind::ON;
  } else { JsonDocument response; response["error"] = "action_must_be_ON_or_OFF"; sendJson(400, response); return; }
  if (command.kind == DashboardCommandKind::OFF) xQueueReset(commands_);
  if (xQueueSend(commands_, &command, 0) != pdPASS) { JsonDocument response; response["error"] = "command_queue_full"; sendJson(503, response); return; }
  ControlResult result;
  bool gotResult = false;
  const uint32_t waitStarted = millis();
  while (millis() - waitStarted < 500) {
    const uint32_t remaining = 500 - (millis() - waitStarted);
    if (xQueueReceive(results_, &result, pdMS_TO_TICKS(remaining)) == pdPASS && result.requestId == command.requestId) { gotResult = true; break; }
  }
  if (!gotResult) { JsonDocument response; response["accepted"] = false; response["reason"] = "controller_ack_timeout"; sendJson(504, response); return; }
  JsonDocument response; response["accepted"] = result.accepted; response["queued"] = false; response["action"] = action;
  if (!result.accepted) response["reason"] = result.reason;
  sendJson(result.accepted ? 202 : 409, response);
}

void HttpApi::handleDevice() {
  PumpStatus snapshot; xQueuePeek(statusSnapshots_, &snapshot, 0);
  JsonDocument response;
  response["device"] = "Pump Controller"; response["firmware"] = "2.3.1";
  response["uptime_ms"] = millis(); response["free_heap"] = ESP.getFreeHeap();
  response["min_free_heap"] = ESP.getMinFreeHeap();
  response["ip"] = WiFi.softAPIP().toString(); response["ap_clients"] = WiFi.softAPgetStationNum();
  response["ap_ssid"] = AP_SSID; response["tank_connected"] = snapshot.sampleFresh;
  response["tank_firmware"] = snapshot.tank.firmware[0] ? snapshot.tank.firmware : "legacy/unknown";
  response["tank_identity_available"] = snapshot.tank.identityAvailable;
  response["tank_boot_id"] = snapshot.tank.bootId;
  response["tank_sequence"] = snapshot.tank.sequence;
  sendJson(200, response);
}

void HttpApi::handleConfig() {
  SystemSettings settings;
  xQueuePeek(settingsQueue_, &settings, 0);
  if (server_.method() == HTTP_POST) {
    JsonDocument request;
    if (deserializeJson(request, server_.arg("plain"))) { JsonDocument e; e["error"]="invalid_json"; sendJson(400,e); return; }
    if (request["auto_start_percent"].is<float>() || request["auto_start_percent"].is<int>()) {
      const float value = request["auto_start_percent"].as<float>();
      if (!isfinite(value) || value < 1.0f || value > 80.0f) { JsonDocument e; e["error"]="auto_start_percent_range_1_to_80"; sendJson(422,e); return; }
      settings.autoStartPercent = value;
    }
    if (request["auto_stop_percent"].is<float>() || request["auto_stop_percent"].is<int>()) {
      const float value = request["auto_stop_percent"].as<float>();
      if (!isfinite(value) || value < 1.0f || value > 100.0f) { JsonDocument e;e["error"]="auto_stop_percent_range_1_to_100";sendJson(422,e);return; }
      settings.autoStopPercent = value;
    }
    if (request["max_runtime_minutes"].is<uint32_t>() || request["max_runtime_minutes"].is<int>()) {
      const uint32_t value = request["max_runtime_minutes"].as<uint32_t>();
      if (value < 1 || value > 60) { JsonDocument e; e["error"]="max_runtime_minutes_range_1_to_60"; sendJson(422,e); return; }
      settings.maxPumpRuntimeMs = value * 60000UL;
    }
    if (request["tank_timeout_seconds"].is<uint32_t>() || request["tank_timeout_seconds"].is<int>()) {
      const uint32_t value = request["tank_timeout_seconds"].as<uint32_t>();
      if (value < 5 || value > 60) { JsonDocument e; e["error"]="tank_timeout_seconds_range_5_to_60"; sendJson(422,e); return; }
      settings.tankTimeoutMs = value * 1000UL;
    }
    if (request["tank_height_cm"].is<float>() || request["tank_height_cm"].is<int>()) {
      const float v=request["tank_height_cm"].as<float>(); if (!isfinite(v)||v<30||v>500) { JsonDocument e;e["error"]="tank_height_cm_range_30_to_500";sendJson(422,e);return;} settings.tankHeightCm=v;
    }
    if (request["distance_empty_cm"].is<float>() || request["distance_empty_cm"].is<int>()) settings.distanceEmptyCm=request["distance_empty_cm"].as<float>();
    if (request["distance_full_cm"].is<float>() || request["distance_full_cm"].is<int>()) settings.distanceFullCm=request["distance_full_cm"].as<float>();
    if (request["sensor_min_distance_cm"].is<float>() || request["sensor_min_distance_cm"].is<int>()) settings.sensorMinDistanceCm=request["sensor_min_distance_cm"].as<float>();
    if (request["sensor_max_distance_cm"].is<float>() || request["sensor_max_distance_cm"].is<int>()) settings.sensorMaxDistanceCm=request["sensor_max_distance_cm"].as<float>();
    if (request["sensor_interval_ms"].is<uint32_t>() || request["sensor_interval_ms"].is<int>()) {
      const uint32_t v=request["sensor_interval_ms"].as<uint32_t>(); if(v<500||v>10000){JsonDocument e;e["error"]="sensor_interval_ms_range_500_to_10000";sendJson(422,e);return;} settings.sensorIntervalMs=v;
    }
    if (request["filter_count"].is<uint8_t>() || request["filter_count"].is<int>()) {
      const uint8_t v=request["filter_count"].as<uint8_t>(); if(v<3||v>9||v%2==0){JsonDocument e;e["error"]="filter_count_must_be_odd_3_to_9";sendJson(422,e);return;} settings.filterCount=v;
    }
    if (settings.autoStopPercent <= settings.autoStartPercent) {
      JsonDocument e; e["error"]="auto_stop_percent_must_exceed_auto_start_percent"; sendJson(422,e); return;
    }
    if (!isfinite(settings.distanceEmptyCm) || !isfinite(settings.distanceFullCm) ||
        !isfinite(settings.sensorMinDistanceCm) || !isfinite(settings.sensorMaxDistanceCm) ||
        settings.distanceEmptyCm <= settings.distanceFullCm ||
        settings.sensorMinDistanceCm < ULTRASONIC_MIN_DISTANCE_CM ||
        settings.distanceFullCm < settings.sensorMinDistanceCm ||
        settings.sensorMinDistanceCm >= settings.sensorMaxDistanceCm ||
        settings.tankTimeoutMs < settings.sensorIntervalMs + 4000UL ||
        settings.sensorMaxDistanceCm > 1000.0f || settings.distanceEmptyCm > settings.sensorMaxDistanceCm) {
      JsonDocument e; e["error"]="invalid_tank_calibration_or_sensor_distance_range"; sendJson(422,e); return;
    }
    xQueueOverwrite(settingsQueue_, &settings);
    systemLog().add("Runtime configuration updated; volatile until reboot");
  }
  JsonDocument response;
  response["auto_start_percent"] = settings.autoStartPercent;
  response["auto_stop_percent"] = settings.autoStopPercent;
  response["max_runtime_minutes"] = settings.maxPumpRuntimeMs / 60000UL;
  response["tank_timeout_seconds"] = settings.tankTimeoutMs / 1000UL;
  response["float_cutoff_enabled"] = ENABLE_FLOAT_SWITCH_CUTOFF;
  response["tank_height_cm"] = settings.tankHeightCm;
  response["distance_empty_cm"] = settings.distanceEmptyCm;
  response["distance_full_cm"] = settings.distanceFullCm;
  response["sensor_min_distance_cm"] = settings.sensorMinDistanceCm;
  response["sensor_max_distance_cm"] = settings.sensorMaxDistanceCm;
  response["sensor_interval_ms"] = settings.sensorIntervalMs;
  response["filter_count"] = settings.filterCount;
  response["saved_to_nvs"] = false;
  sendJson(200, response);
}

void HttpApi::handleDashboard() { sendDashboard(server_); }

void HttpApi::handleLogs() {
  LogEntry entries[24];
  const uint8_t count = systemLog().copy(entries, 24);
  JsonDocument response;
  JsonArray items = response["items"].to<JsonArray>();
  for (uint8_t i = 0; i < count; ++i) {
    JsonObject item = items.add<JsonObject>();
    item["uptime_ms"] = entries[i].uptimeMs;
    item["message"] = entries[i].message;
  }
  sendJson(200, response);
}

void HttpApi::handleRestart() {
  DashboardCommand command; command.kind = DashboardCommandKind::RESTART;
  xQueueReset(commands_);
  if (xQueueSend(commands_, &command, 0) != pdPASS) {
    JsonDocument response; response["error"] = "command_queue_full"; sendJson(503, response); return;
  }
  systemLog().add("Safe restart requested");
  JsonDocument response;
  response["accepted"] = true;
  response["message"] = "SSR is set OFF before reboot";
  sendJson(202, response);
}

void HttpApi::handleNotFound() {
  Serial.printf("[HTTP] 404: %s %s\n", server_.method() == HTTP_GET ? "GET" : "request",
                server_.uri().c_str());
  JsonDocument response;
  response["error"] = "not_found";
  sendJson(404, response);
}

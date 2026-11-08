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
  SystemSettings persistedSettings;
  pumpConfigSaved_ = ConfigStore::load(persistedSettings);

  const char* headers[] = {"Content-Type"};
  server_.collectHeaders(headers, 1);
  server_.on("/api/v1/tank", HTTP_POST, [this]() { handleTankPost(); });
  server_.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/v1/control", HTTP_POST, [this]() { handleControl(); });
  server_.on("/api/v1/device", HTTP_GET, [this]() { handleDevice(); });
  server_.on("/api/v1/logs", HTTP_GET, [this]() { handleLogs(); });
  server_.on("/api/v1/config", HTTP_GET, [this]() { handleConfig(); });
  server_.on("/api/v1/config", HTTP_POST, [this]() { handleConfig(); });
  server_.on("/api/v1/config/restore-defaults", HTTP_POST, [this]() { handleConfig(); });
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

uint32_t HttpApi::settingsVersion(const SystemSettings& s) const {
  uint32_t hash = 2166136261UL;
  auto mix = [&hash](uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) { hash ^= (value >> (i * 8)) & 0xffU; hash *= 16777619UL; }
  };
  mix(static_cast<uint32_t>(lroundf(s.autoStartPercent * 100.0f)));
  mix(static_cast<uint32_t>(lroundf(s.autoStopPercent * 100.0f)));
  mix(s.maxPumpRuntimeMs); mix(s.tankTimeoutMs);
  mix(static_cast<uint32_t>(lroundf(s.tankHeightCm * 100.0f)));
  mix(static_cast<uint32_t>(lroundf(s.distanceEmptyCm * 100.0f)));
  mix(static_cast<uint32_t>(lroundf(s.distanceFullCm * 100.0f)));
  mix(static_cast<uint32_t>(lroundf(s.sensorMinDistanceCm * 100.0f)));
  mix(static_cast<uint32_t>(lroundf(s.sensorMaxDistanceCm * 100.0f)));
  mix(s.sensorIntervalMs); mix(s.filterCount);
  return hash == 0 ? 1 : hash;
}

void HttpApi::addTankConfig(JsonDocument& response, const SystemSettings& s, uint32_t version) {
  JsonObject c = response["tank_config"].to<JsonObject>();
  c["tank_height_cm"] = s.tankHeightCm;
  c["distance_empty_cm"] = s.distanceEmptyCm;
  c["distance_full_cm"] = s.distanceFullCm;
  c["sensor_min_distance_cm"] = s.sensorMinDistanceCm;
  c["sensor_max_distance_cm"] = s.sensorMaxDistanceCm;
  c["sensor_interval_ms"] = s.sensorIntervalMs;
  c["filter_count"] = s.filterCount;
  c["config_version"] = version;
}

void HttpApi::handleTankPost() {
  if (configSavePending_ && millis() - configPendingSince_ > 30000UL) {
    configSavePending_ = false;
    configSaveFailed_ = true;
    systemLog().add("Configuration save failed: Tank NVS acknowledgement timeout");
  }
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
  if (!request["config_version"].isUnbound() && !request["config_version"].is<uint32_t>()) {
    JsonDocument response; response["error"] = "config_version_must_be_uint32"; sendJson(400, response); return;
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

  const uint32_t tankConfigVersion = request["config_version"] | 0UL;
  if (tankConfigVersion != 0) {
    lastTankConfigVersion_ = tankConfigVersion;
    if (configSavePending_ && tankConfigVersion == pendingSettingsVersion_) {
      if (ConfigStore::save(pendingSettings_)) {
        xQueueOverwrite(settingsQueue_, &pendingSettings_);
        pumpConfigSaved_ = true;
        systemLog().add("Pump and Tank configuration saved to NVS");
        Serial.printf("[CONFIG] NVS transaction %lu committed on both controllers\n", static_cast<unsigned long>(tankConfigVersion));
        configSavePending_ = false;
        configSaveFailed_ = false;
      } else {
        configSavePending_ = false;
        configSaveFailed_ = true;
        pumpConfigSaved_ = false;
        systemLog().add("Pump NVS write failed after Tank saved configuration");
        Serial.println("[CONFIG][ERROR] Pump NVS write failed; Tank has acknowledged the new configuration");
      }
    }
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
  SystemSettings activeSettings;
  xQueuePeek(settingsQueue_, &activeSettings, 0);
  const bool tankConfigCoherent = tankConfigVersion == 0 ||
      tankConfigVersion == settingsVersion(activeSettings);
  sample.valid = request["sensor_valid"].as<bool>() && tankConfigCoherent;
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
  const SystemSettings& desired = configSavePending_ ? pendingSettings_ : currentSettings;
  const uint32_t desiredVersion = configSavePending_ ? pendingSettingsVersion_ : settingsVersion(currentSettings);
  addTankConfig(response, desired, desiredVersion);
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
  response["device"] = "Pump Controller"; response["firmware"] = "2.4.0";
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
  if (configSavePending_ && millis() - configPendingSince_ > 30000UL) {
    configSavePending_ = false;
    configSaveFailed_ = true;
    systemLog().add("Configuration save failed: Tank NVS acknowledgement timeout");
  }
  if (server_.method() == HTTP_POST) {
    if (server_.uri() == "/api/v1/config/restore-defaults") {
      settings = ConfigStore::defaults();
    } else {
    JsonDocument request;
    if (deserializeJson(request, server_.arg("plain"))) { JsonDocument e; e["error"]="invalid_json"; sendJson(400,e); return; }
    const char* numericFields[] = {"auto_start_percent", "auto_stop_percent", "max_runtime_minutes", "tank_timeout_seconds", "tank_height_cm", "distance_empty_cm", "distance_full_cm", "sensor_min_distance_cm", "sensor_max_distance_cm", "sensor_interval_ms", "filter_count"};
    for (const char* key : numericFields) {
      JsonVariantConst value = request[key];
      if (!value.isUnbound() && !value.is<float>() && !value.is<int>() && !value.is<uint32_t>()) {
        JsonDocument e; e["error"] = "configuration_fields_must_be_numeric"; e["field"] = key; sendJson(422, e); return;
      }
    }
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
    if (!request["max_runtime_minutes"].isUnbound()) {
      const float value = request["max_runtime_minutes"].as<float>();
      if (!isfinite(value) || floorf(value) != value || value < 1 || value > 60) { JsonDocument e; e["error"]="max_runtime_minutes_integer_range_1_to_60"; sendJson(422,e); return; }
      settings.maxPumpRuntimeMs = static_cast<uint32_t>(value) * 60000UL;
    }
    if (!request["tank_timeout_seconds"].isUnbound()) {
      const float value = request["tank_timeout_seconds"].as<float>();
      if (!isfinite(value) || floorf(value) != value || value < 5 || value > 60) { JsonDocument e; e["error"]="tank_timeout_seconds_integer_range_5_to_60"; sendJson(422,e); return; }
      settings.tankTimeoutMs = static_cast<uint32_t>(value) * 1000UL;
    }
    if (request["tank_height_cm"].is<float>() || request["tank_height_cm"].is<int>()) {
      const float v=request["tank_height_cm"].as<float>(); if (!isfinite(v)||v<30||v>500) { JsonDocument e;e["error"]="tank_height_cm_range_30_to_500";sendJson(422,e);return;} settings.tankHeightCm=v;
    }
    if (request["distance_empty_cm"].is<float>() || request["distance_empty_cm"].is<int>()) settings.distanceEmptyCm=request["distance_empty_cm"].as<float>();
    if (request["distance_full_cm"].is<float>() || request["distance_full_cm"].is<int>()) settings.distanceFullCm=request["distance_full_cm"].as<float>();
    if (request["sensor_min_distance_cm"].is<float>() || request["sensor_min_distance_cm"].is<int>()) settings.sensorMinDistanceCm=request["sensor_min_distance_cm"].as<float>();
    if (request["sensor_max_distance_cm"].is<float>() || request["sensor_max_distance_cm"].is<int>()) settings.sensorMaxDistanceCm=request["sensor_max_distance_cm"].as<float>();
    if (!request["sensor_interval_ms"].isUnbound()) {
      const float v=request["sensor_interval_ms"].as<float>(); if(!isfinite(v)||floorf(v)!=v||v<500||v>10000){JsonDocument e;e["error"]="sensor_interval_ms_integer_range_500_to_10000";sendJson(422,e);return;} settings.sensorIntervalMs=static_cast<uint32_t>(v);
    }
    if (!request["filter_count"].isUnbound()) {
      const float v=request["filter_count"].as<float>(); if(!isfinite(v)||floorf(v)!=v||!(v==3||v==5||v==7||v==9)){JsonDocument e;e["error"]="filter_count_must_be_3_5_7_or_9";sendJson(422,e);return;} settings.filterCount=static_cast<uint8_t>(v);
    }
    if (settings.autoStopPercent <= settings.autoStartPercent) {
      JsonDocument e; e["error"]="auto_stop_percent_must_exceed_auto_start_percent"; sendJson(422,e); return;
    }
    }
    if (!ConfigStore::validate(settings)) {
      JsonDocument e; e["error"]="configuration_validation_failed"; sendJson(422,e); return;
    }
    if (configSavePending_) { JsonDocument e; e["error"]="configuration_save_in_progress"; sendJson(409,e); return; }
    PumpStatus snapshot;
    if (xQueuePeek(statusSnapshots_, &snapshot, 0) != pdPASS || !snapshot.sampleFresh || !snapshot.tank.valid) {
      JsonDocument e; e["error"]="tank_controller_must_be_connected_with_fresh_valid_data"; sendJson(503,e); return;
    }
    pendingSettings_ = settings;
    pendingSettingsVersion_ = settingsVersion(settings);
    configPendingSince_ = millis();
    configSavePending_ = true;
    configSaveFailed_ = false;
    systemLog().add("Configuration save staged; waiting for Tank NVS acknowledgement");
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
  const uint32_t activeVersion = settingsVersion(settings);
  const bool bothSaved = pumpConfigSaved_ && lastTankConfigVersion_ == activeVersion;
  response["saved_to_nvs"] = !configSavePending_ && !configSaveFailed_ && bothSaved;
  response["save_state"] = configSavePending_ ? "pending_tank_ack" : configSaveFailed_ ? "failed" : bothSaved ? "saved" : !pumpConfigSaved_ ? "unsaved_defaults" : "tank_sync_required";
  response["tank_config_saved"] = lastTankConfigVersion_ == activeVersion;
  response["config_version"] = configSavePending_ ? pendingSettingsVersion_ : activeVersion;
  if (configSavePending_) {
    response["pump_config_saved"] = false;
    response["tank_config_saved"] = lastTankConfigVersion_ == pendingSettingsVersion_;
    response["message"] = "Menunggu Tank Controller menyimpan konfigurasi ke NVS";
  } else {
    response["pump_config_saved"] = pumpConfigSaved_;
  }
  sendJson(server_.method() == HTTP_POST && configSavePending_ ? 202 : 200, response);
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

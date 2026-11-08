#include "config_store.h"
#include <Preferences.h>
#include <math.h>
#include "config.h"

SystemSettings ConfigStore::defaults() {
  SystemSettings s;
  s.autoStartPercent = AUTO_START_LEVEL_PERCENT;
  s.autoStopPercent = AUTO_STOP_LEVEL_DEFAULT_PERCENT;
  s.maxPumpRuntimeMs = MAX_PUMP_RUNTIME_MS;
  s.tankTimeoutMs = SENSOR_TIMEOUT_MS;
  s.tankHeightCm = 120.0f;
  s.distanceEmptyCm = 100.0f;
  s.distanceFullCm = 25.0f;
  s.sensorMinDistanceCm = ULTRASONIC_MIN_DISTANCE_CM;
  s.sensorMaxDistanceCm = 450.0f;
  s.sensorIntervalMs = 1000UL;
  s.filterCount = 5;
  return s;
}

bool ConfigStore::validate(const SystemSettings& s) {
  return isfinite(s.autoStartPercent) && s.autoStartPercent >= 1.0f && s.autoStartPercent <= 80.0f &&
      isfinite(s.autoStopPercent) && s.autoStopPercent > s.autoStartPercent && s.autoStopPercent <= 100.0f &&
      s.maxPumpRuntimeMs >= 60000UL && s.maxPumpRuntimeMs <= 3600000UL &&
      s.tankTimeoutMs >= 5000UL && s.tankTimeoutMs <= 60000UL &&
      isfinite(s.tankHeightCm) && s.tankHeightCm >= 30.0f && s.tankHeightCm <= 500.0f &&
      isfinite(s.distanceEmptyCm) && isfinite(s.distanceFullCm) &&
      isfinite(s.sensorMinDistanceCm) && isfinite(s.sensorMaxDistanceCm) &&
      s.distanceEmptyCm > s.distanceFullCm && s.sensorMinDistanceCm >= ULTRASONIC_MIN_DISTANCE_CM &&
      s.distanceFullCm >= s.sensorMinDistanceCm && s.sensorMinDistanceCm < s.sensorMaxDistanceCm &&
      s.sensorMaxDistanceCm <= 1000.0f && s.distanceEmptyCm <= s.sensorMaxDistanceCm &&
      s.sensorIntervalMs >= 500UL && s.sensorIntervalMs <= 10000UL &&
      s.tankTimeoutMs >= s.sensorIntervalMs + 4000UL &&
      (s.filterCount == 3 || s.filterCount == 5 || s.filterCount == 7 || s.filterCount == 9);
}

bool ConfigStore::load(SystemSettings& s) {
  s = defaults();
  Preferences p;
  if (!p.begin("pumpcfg", true)) return false;
  const bool present = p.getUChar("schema", 0) == 1;
  if (present) {
    s.autoStartPercent = p.getFloat("start", s.autoStartPercent);
    s.autoStopPercent = p.getFloat("stop", s.autoStopPercent);
    s.maxPumpRuntimeMs = p.getUInt("runtime", s.maxPumpRuntimeMs);
    s.tankTimeoutMs = p.getUInt("timeout", s.tankTimeoutMs);
    s.tankHeightCm = p.getFloat("height", s.tankHeightCm);
    s.distanceEmptyCm = p.getFloat("empty", s.distanceEmptyCm);
    s.distanceFullCm = p.getFloat("full", s.distanceFullCm);
    s.sensorMinDistanceCm = p.getFloat("min", s.sensorMinDistanceCm);
    s.sensorMaxDistanceCm = p.getFloat("max", s.sensorMaxDistanceCm);
    s.sensorIntervalMs = p.getUInt("interval", s.sensorIntervalMs);
    s.filterCount = p.getUChar("filter", s.filterCount);
  }
  p.end();
  if (!present || !validate(s)) { s = defaults(); return false; }
  return true;
}

bool ConfigStore::save(const SystemSettings& s) {
  if (!validate(s)) return false;
  Preferences p;
  if (!p.begin("pumpcfg", false)) return false;
  bool ok = p.putUChar("schema", 0) == sizeof(uint8_t) &&
      p.putFloat("start", s.autoStartPercent) == sizeof(float) &&
      p.putFloat("stop", s.autoStopPercent) == sizeof(float) &&
      p.putUInt("runtime", s.maxPumpRuntimeMs) == sizeof(uint32_t) &&
      p.putUInt("timeout", s.tankTimeoutMs) == sizeof(uint32_t) &&
      p.putFloat("height", s.tankHeightCm) == sizeof(float) &&
      p.putFloat("empty", s.distanceEmptyCm) == sizeof(float) &&
      p.putFloat("full", s.distanceFullCm) == sizeof(float) &&
      p.putFloat("min", s.sensorMinDistanceCm) == sizeof(float) &&
      p.putFloat("max", s.sensorMaxDistanceCm) == sizeof(float) &&
      p.putUInt("interval", s.sensorIntervalMs) == sizeof(uint32_t) &&
      p.putUChar("filter", s.filterCount) == sizeof(uint8_t);
  if (ok) ok = p.putUChar("schema", 1) == sizeof(uint8_t);
  p.end();
  return ok;
}

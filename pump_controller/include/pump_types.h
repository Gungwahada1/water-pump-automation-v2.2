#pragma once

#include <Arduino.h>

enum class PumpMode : uint8_t { AUTO_MODE, MANUAL_MODE };
enum class StopReason : uint8_t {
  NONE, NO_DATA, STALE_DATA, INVALID_SENSOR, FLOAT_FULL, ULTRASONIC_FULL,
  MANUAL_STOP, RUNTIME_LIMIT
};

struct TankSample {
  bool received = false;
  bool valid = false;
  bool floatFull = false;
  float levelPercent = 0.0f;
  float distanceCm = 0.0f;
  bool distanceAvailable = false;
  uint32_t sequence = 0;
  uint32_t bootId = 0;
  bool identityAvailable = false;
  char firmware[16] = {};
  uint32_t receivedAt = 0;
};

struct PhysicalInputs {
  PumpMode mode = PumpMode::AUTO_MODE;
  bool pumpOnPressed = false;
  bool pumpOffPressed = false;
  bool dashboardOnRequested = false;
  bool dashboardOffRequested = false;
};

struct PumpStatus {
  TankSample tank;
  PumpMode mode = PumpMode::AUTO_MODE;
  StopReason stopReason = StopReason::NO_DATA;
  bool pumpOn = false;
  bool runtimeLockout = false;
  bool sampleFresh = false;
  uint32_t sampleAgeMs = 0;
  uint32_t pumpRunMs = 0;
  uint32_t maxPumpRuntimeMs = 1500000UL;
  float autoStartLevelPercent = 20.0f;
  float autoStopLevelPercent = 100.0f;
};

enum class DashboardCommandKind : uint8_t { ON, OFF, RESTART };
struct DashboardCommand { DashboardCommandKind kind = DashboardCommandKind::OFF; uint32_t requestId = 0; };
struct ControlResult { uint32_t requestId = 0; bool accepted = false; char reason[48] = {}; };
struct SystemSettings {
  float autoStartPercent = 20.0f;
  float autoStopPercent = 100.0f;
  uint32_t maxPumpRuntimeMs = 1500000UL;
  uint32_t tankTimeoutMs = 15000UL;
  float tankHeightCm = 120.0f;
  float distanceEmptyCm = 100.0f;
  float distanceFullCm = 25.0f;
  float sensorMinDistanceCm = 20.0f;
  float sensorMaxDistanceCm = 450.0f;
  uint32_t sensorIntervalMs = 1000UL;
  uint8_t filterCount = 5;
};

inline const char* modeName(PumpMode mode) {
  return mode == PumpMode::AUTO_MODE ? "AUTO" : "MANUAL";
}

inline const char* stopReasonName(StopReason reason) {
  switch (reason) {
    case StopReason::NO_DATA: return "no_data";
    case StopReason::STALE_DATA: return "stale_data";
    case StopReason::INVALID_SENSOR: return "invalid_sensor";
    case StopReason::FLOAT_FULL: return "float_full";
    case StopReason::ULTRASONIC_FULL: return "ultrasonic_stop_threshold";
    case StopReason::MANUAL_STOP: return "manual_stop";
    case StopReason::RUNTIME_LIMIT: return "runtime_limit";
    default: return "none";
  }
}

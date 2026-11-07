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
  uint32_t receivedAt = 0;
};

struct PhysicalInputs {
  PumpMode mode = PumpMode::AUTO_MODE;
  bool pumpOnPressed = false;
  bool pumpOffPressed = false;
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
    case StopReason::ULTRASONIC_FULL: return "ultrasonic_100_percent";
    case StopReason::MANUAL_STOP: return "manual_stop";
    case StopReason::RUNTIME_LIMIT: return "runtime_limit";
    default: return "none";
  }
}

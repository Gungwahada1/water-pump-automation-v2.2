#pragma once

#include <Arduino.h>

struct TankReading {
  float distanceCm = 0.0f;
  float levelPercent = 0.0f;
  bool ultrasonicValid = false;
  bool floatFull = false;
  uint32_t sequence = 0;
};

struct TankRuntimeConfig {
  float tankHeightCm = 120.0f;
  float distanceEmptyCm = 100.0f;
  float distanceFullCm = 25.0f;
  float minDistanceCm = 20.0f;
  float maxDistanceCm = 450.0f;
  uint32_t sampleIntervalMs = 1000UL;
  uint8_t filterCount = 5;
};

class TankSensors {
 public:
  void begin();
  TankReading read(const TankRuntimeConfig& config);

 private:
  bool readFloatFull();
  bool readDistanceCm(float& distanceCm, const TankRuntimeConfig& config);
  bool readMedianDistance(float& distanceCm, const TankRuntimeConfig& config);

  bool floatStableFull_ = false;
  bool floatLastRawFull_ = false;
  uint32_t floatChangedAt_ = 0;
};

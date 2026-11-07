#pragma once

#include <Arduino.h>

struct TankReading {
  float distanceCm = 0.0f;
  float levelPercent = 0.0f;
  bool ultrasonicValid = false;
  bool floatFull = false;
};

class TankSensors {
 public:
  void begin();
  TankReading read();

 private:
  bool readFloatFull();
  bool readDistanceCm(float& distanceCm);
  bool readMedianDistance(float& distanceCm);

  bool floatStableFull_ = false;
  bool floatLastRawFull_ = false;
  uint32_t floatChangedAt_ = 0;
};

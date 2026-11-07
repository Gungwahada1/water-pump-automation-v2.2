#pragma once

#include <Arduino.h>
#include "pump_types.h"

class DebouncedButton {
 public:
  void begin(uint8_t pin);
  void update();
  bool wasPressed() const { return pressedEdge_; }

 private:
  uint8_t pin_ = 0;
  bool stableHigh_ = true;
  bool lastRawHigh_ = true;
  bool pressedEdge_ = false;
  uint32_t changedAt_ = 0;
};

class InputManager {
 public:
  void begin();
  PhysicalInputs read();

 private:
  DebouncedButton onButton_;
  DebouncedButton offButton_;
  DebouncedButton resetButton_;
};

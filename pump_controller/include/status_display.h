#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include "pump_types.h"

class StatusDisplay {
 public:
  bool begin(QueueHandle_t statusSnapshots);

 private:
  static void taskEntry(void* argument);
  void runDisplayTask();
  void render(const PumpStatus& status);
  void writeLine(uint8_t row, const char* text);

  QueueHandle_t statusSnapshots_ = nullptr;
  hd44780_I2Cexp lcd_; // auto-detect common PCF8574/MCP23008 backpacks
  bool initialized_ = false;
  char lastLines_[4][21] = {};
};

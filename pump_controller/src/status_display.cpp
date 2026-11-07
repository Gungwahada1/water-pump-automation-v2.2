#include "status_display.h"
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include "config.h"

bool StatusDisplay::begin(QueueHandle_t statusSnapshots) {
  statusSnapshots_ = statusSnapshots;
  if (statusSnapshots_ == nullptr) return false;

  const BaseType_t started = xTaskCreatePinnedToCore(
      taskEntry, "pump_lcd", 4096, this, 1, nullptr, 0);
  return started == pdPASS;
}

void StatusDisplay::taskEntry(void* argument) {
  static_cast<StatusDisplay*>(argument)->runDisplayTask();
}

void StatusDisplay::runDisplayTask() {
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  Serial.printf("[LCD] I2C started: SDA=GPIO%d SCL=GPIO%d, display=%ux%u\n",
                PIN_LCD_SDA, PIN_LCD_SCL, LCD_COLUMNS, LCD_ROWS);

  for (;;) {
    if (!initialized_) {
      const int result = lcd_.begin(LCD_COLUMNS, LCD_ROWS);
      if (result == 0) {
        lcd_.backlight();
        lcd_.clear();
        memset(lastLines_, 0, sizeof(lastLines_));
        initialized_ = true;
        Serial.println("[LCD] 20x4 initialized; I2C backpack detected");
      } else {
        Serial.printf("[LCD][ERROR] initialization failed, code=%d; retrying\n", result);
      }
    }

    if (initialized_) {
      PumpStatus status;
      if (xQueuePeek(statusSnapshots_, &status, 0) == pdPASS) render(status);
    }
    vTaskDelay(pdMS_TO_TICKS(initialized_ ? LCD_REFRESH_INTERVAL_MS : 3000UL));
  }
}

void StatusDisplay::writeLine(uint8_t row, const char* text) {
  char padded[LCD_COLUMNS + 1];
  memset(padded, ' ', LCD_COLUMNS);
  padded[LCD_COLUMNS] = '\0';
  strncpy(padded, text, LCD_COLUMNS);
  if (strncmp(lastLines_[row], padded, LCD_COLUMNS) == 0) return;

  // Explicitly erase all 20 cells first. lcd_.print() stops at the first NUL,
  // so printing a padded C string alone would leave old trailing characters.
  lcd_.setCursor(0, row);
  for (uint8_t column = 0; column < LCD_COLUMNS; ++column) lcd_.write(' ');
  lcd_.setCursor(0, row);
  lcd_.print(padded);
  memcpy(lastLines_[row], padded, sizeof(padded));
}

void StatusDisplay::render(const PumpStatus& status) {
  char line[4][LCD_COLUMNS + 1] = {};
  const TankSample& tank = status.tank;

  snprintf(line[0], sizeof(line[0]), "MODE:%s PUMP:%s",
           modeName(status.mode), status.pumpOn ? "ON" : "OFF");

  if (!tank.received) {
    snprintf(line[1], sizeof(line[1]), "LEVEL: --.-%%");
    snprintf(line[2], sizeof(line[2]), "TANK: NO DATA");
  } else if (!tank.valid) {
    snprintf(line[1], sizeof(line[1]), "LEVEL: --.-%% INVALID");
    snprintf(line[2], sizeof(line[2]), "TANK: SENSOR ERROR");
  } else {
    const char* levelTag = tank.levelPercent >= ULTRASONIC_FULL_STOP_PERCENT
        ? "FULL" : tank.levelPercent <= AUTO_START_LEVEL_PERCENT ? "LOW" : "";
    snprintf(line[1], sizeof(line[1]), "LEVEL:%5.1f%% %s",
             tank.levelPercent, levelTag);
    if (status.sampleFresh) {
      const unsigned ageSeconds = static_cast<unsigned>(status.sampleAgeMs / 1000UL);
      snprintf(line[2], sizeof(line[2]), "TANK: OK AGE:%lus", ageSeconds);
    } else {
      const unsigned ageSeconds = static_cast<unsigned>(status.sampleAgeMs / 1000UL);
      snprintf(line[2], sizeof(line[2]), "TANK: STALE %lus", ageSeconds);
    }
  }

  if (status.pumpOn) {
    const unsigned runSeconds = static_cast<unsigned>(status.pumpRunMs / 1000UL);
    snprintf(line[3], sizeof(line[3]), "RUN:%02u:%02u / 30:00",
             (runSeconds / 60U) % 100U, runSeconds % 60U);
  } else if (status.runtimeLockout) {
    snprintf(line[3], sizeof(line[3]), "STOP: 30M LOCKOUT");
  } else {
    switch (status.stopReason) {
      case StopReason::NO_DATA: snprintf(line[3], sizeof(line[3]), "STOP: WAIT TANK"); break;
      case StopReason::STALE_DATA: snprintf(line[3], sizeof(line[3]), "STOP: DATA STALE"); break;
      case StopReason::INVALID_SENSOR: snprintf(line[3], sizeof(line[3]), "STOP: US INVALID"); break;
      case StopReason::FLOAT_FULL: snprintf(line[3], sizeof(line[3]), "STOP: FLOAT FULL"); break;
      case StopReason::ULTRASONIC_FULL: snprintf(line[3], sizeof(line[3]), "STOP: ULTRASONIC"); break;
      case StopReason::MANUAL_STOP: snprintf(line[3], sizeof(line[3]), "MANUAL: OFF"); break;
      case StopReason::RUNTIME_LIMIT: snprintf(line[3], sizeof(line[3]), "STOP: 30M LOCKOUT"); break;
      case StopReason::NONE:
      default:
        if (status.mode == PumpMode::AUTO_MODE)
          snprintf(line[3], sizeof(line[3]), "AUTO: WAIT <=20%%");
        else
          snprintf(line[3], sizeof(line[3]), "MANUAL: PRESS ON");
        break;
    }
  }

  for (uint8_t row = 0; row < LCD_ROWS; ++row) writeLine(row, line[row]);
}

#pragma once

#include <Arduino.h>

// Network
static constexpr char AP_SSID[] = "PumpCtrl-Setup";
static constexpr char AP_PASSWORD[] = "pumpcontrol22"; // >= 8 characters
static constexpr uint8_t AP_CHANNEL = 6;
static constexpr uint8_t AP_MAX_CLIENTS = 2;

// Pin mapping: ESP32 DevKit V1 / ESP32-WROOM-32
static constexpr uint8_t PIN_SSR = 25;
static constexpr uint8_t PIN_BUTTON_ON = 26;
static constexpr uint8_t PIN_BUTTON_OFF = 27;
static constexpr uint8_t PIN_MODE_SELECT = 14; // LOW = MANUAL, HIGH/open = AUTO
static constexpr uint8_t PIN_BUTTON_RESET = 33; // NO to GND; software restart
static constexpr uint8_t PIN_STATUS_LED = 2;
static constexpr uint8_t PIN_LCD_SDA = 21;
static constexpr uint8_t PIN_LCD_SCL = 22;
static constexpr uint8_t LCD_COLUMNS = 20;
static constexpr uint8_t LCD_ROWS = 4;

// Verify against the actual SSR input module before connecting any load.
static constexpr uint8_t SSR_ACTIVE_LEVEL = HIGH;
static constexpr uint8_t SSR_OFF_LEVEL = (SSR_ACTIVE_LEVEL == HIGH) ? LOW : HIGH;

// Control thresholds and safety timers
// Keep disabled until a physical float switch is installed and verified.
static constexpr bool ENABLE_FLOAT_SWITCH_CUTOFF = false;
static constexpr float AUTO_START_LEVEL_PERCENT = 20.0f;
static constexpr float AUTO_STOP_LEVEL_DEFAULT_PERCENT = 100.0f;
static constexpr float ULTRASONIC_LEVEL_FULL_PERCENT = 100.0f;
static constexpr float ULTRASONIC_MIN_DISTANCE_CM = 20.0f;
static constexpr uint32_t SENSOR_TIMEOUT_MS = 15000UL;
static constexpr uint32_t MAX_PUMP_RUNTIME_MS = 25UL * 60UL * 1000UL;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 15UL;
static constexpr uint32_t HTTP_CLIENT_TIMEOUT_MS = 1500UL;
static constexpr uint32_t DEBUG_STATUS_INTERVAL_MS = 3000UL;
static constexpr uint32_t LCD_REFRESH_INTERVAL_MS = 500UL;

#pragma once

#include <Arduino.h>

// Pump Controller SoftAP from Stage 1
static constexpr char WIFI_SSID[] = "PumpCtrl-Setup";
static constexpr char WIFI_PASSWORD[] = "pumpcontrol22";
static constexpr char PUMP_CONTROLLER_IP[] = "192.168.4.1";
static constexpr uint16_t PUMP_CONTROLLER_PORT = 80;
static constexpr char TANK_POST_PATH[] = "/api/v1/tank";

// ESP32 DevKit V1 / ESP32-WROOM-32 pins
static constexpr uint8_t PIN_ULTRASONIC_TRIG = 25;
static constexpr uint8_t PIN_ULTRASONIC_ECHO = 26;
static constexpr uint8_t PIN_FLOAT_SWITCH = 27;
static constexpr uint8_t PIN_STATUS_LED = 2;

// Temporarily disable the float switch until the physical switch is installed.
static constexpr bool ENABLE_FLOAT_SWITCH = false;

// Float switch assumptions: contact closed at low float, open at full/high float.
// Verify with a multimeter. With INPUT_PULLUP and switch wired to GND,
// open contact reads HIGH.
static constexpr uint8_t FLOAT_FULL_LEVEL = HIGH;
static constexpr uint32_t FLOAT_DEBOUNCE_MS = 50UL;

// IMPORTANT: measure these distances from the ultrasonic sensor face to water.
// Replace these example values with actual tank calibration before relying on %.
static constexpr float DISTANCE_AT_EMPTY_CM = 100.0f;
static constexpr float DISTANCE_AT_FULL_CM = 25.0f;

static constexpr uint32_t ULTRASONIC_TIMEOUT_US = 30000UL;
static constexpr float ULTRASONIC_MIN_DISTANCE_CM = 20.0f;
static constexpr float ULTRASONIC_MAX_DISTANCE_CM = 450.0f;
static constexpr uint8_t LEVEL_SAMPLE_COUNT = 5;
static constexpr uint32_t SENSOR_READ_INTERVAL_MS = 1000UL;
static constexpr uint32_t HTTP_POST_INTERVAL_MS = 1000UL;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
static constexpr uint32_t HTTP_TIMEOUT_MS = 2000UL;

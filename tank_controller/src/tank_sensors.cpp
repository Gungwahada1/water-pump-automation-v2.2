#include "tank_sensors.h"
#include "config.h"

void TankSensors::begin() {
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);
  if (ENABLE_FLOAT_SWITCH) {
    pinMode(PIN_FLOAT_SWITCH, INPUT_PULLUP);
    floatStableFull_ = floatLastRawFull_ = digitalRead(PIN_FLOAT_SWITCH) == FLOAT_FULL_LEVEL;
    floatChangedAt_ = millis();
  }
}

bool TankSensors::readFloatFull() {
  if (!ENABLE_FLOAT_SWITCH) return false;

  const bool rawFull = digitalRead(PIN_FLOAT_SWITCH) == FLOAT_FULL_LEVEL;
  if (rawFull != floatLastRawFull_) {
    floatLastRawFull_ = rawFull;
    floatChangedAt_ = millis();
  }
  if (rawFull != floatStableFull_ && millis() - floatChangedAt_ >= FLOAT_DEBOUNCE_MS)
    floatStableFull_ = rawFull;
  return floatStableFull_;
}

bool TankSensors::readDistanceCm(float& distanceCm) {
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(3);
  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  const uint32_t duration = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, ULTRASONIC_TIMEOUT_US);
  if (duration == 0) return false;
  distanceCm = duration * 0.0343f / 2.0f;
  return isfinite(distanceCm) && distanceCm >= ULTRASONIC_MIN_DISTANCE_CM &&
         distanceCm <= ULTRASONIC_MAX_DISTANCE_CM;
}

bool TankSensors::readMedianDistance(float& distanceCm) {
  float values[LEVEL_SAMPLE_COUNT];
  uint8_t count = 0;
  for (uint8_t i = 0; i < LEVEL_SAMPLE_COUNT; ++i) {
    float reading;
    if (readDistanceCm(reading)) values[count++] = reading;
    delay(30); // reduce echo cross-talk between pings
  }
  if (count < 3) return false;

  for (uint8_t i = 1; i < count; ++i) {
    const float value = values[i];
    int8_t j = static_cast<int8_t>(i) - 1;
    while (j >= 0 && values[j] > value) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = value;
  }
  distanceCm = values[count / 2];
  return true;
}

TankReading TankSensors::read() {
  TankReading result;
  result.floatFull = readFloatFull();

  const bool calibrationValid = DISTANCE_AT_EMPTY_CM > DISTANCE_AT_FULL_CM &&
      DISTANCE_AT_FULL_CM > 0.0f;
  float distance;
  if (!calibrationValid || !readMedianDistance(distance)) return result;

  result.distanceCm = distance;
  float level = (DISTANCE_AT_EMPTY_CM - distance) * 100.0f /
                (DISTANCE_AT_EMPTY_CM - DISTANCE_AT_FULL_CM);
  if (level < 0.0f) level = 0.0f;
  if (level > 100.0f) level = 100.0f;
  result.levelPercent = level;
  result.ultrasonicValid = true;
  return result;
}

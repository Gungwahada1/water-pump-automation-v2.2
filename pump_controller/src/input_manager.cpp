#include "input_manager.h"
#include "config.h"

void DebouncedButton::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
  stableHigh_ = lastRawHigh_ = digitalRead(pin_) == HIGH;
  changedAt_ = millis();
}

void DebouncedButton::update() {
  pressedEdge_ = false;
  const bool rawHigh = digitalRead(pin_) == HIGH;
  if (rawHigh != lastRawHigh_) {
    lastRawHigh_ = rawHigh;
    changedAt_ = millis();
  }
  if (rawHigh != stableHigh_ && millis() - changedAt_ >= BUTTON_DEBOUNCE_MS) {
    stableHigh_ = rawHigh;
    pressedEdge_ = !stableHigh_;
  }
}

void InputManager::begin() {
  onButton_.begin(PIN_BUTTON_ON);
  offButton_.begin(PIN_BUTTON_OFF);
  resetButton_.begin(PIN_BUTTON_RESET);
  pinMode(PIN_MODE_SELECT, INPUT_PULLUP);
}

PhysicalInputs InputManager::read() {
  onButton_.update();
  offButton_.update();
  resetButton_.update();
  PhysicalInputs result;
  result.mode = digitalRead(PIN_MODE_SELECT) == LOW
      ? PumpMode::MANUAL_MODE : PumpMode::AUTO_MODE;
  result.pumpOnPressed = onButton_.wasPressed();
  result.pumpOffPressed = offButton_.wasPressed();
  result.resetPressed = resetButton_.wasPressed();
  return result;
}

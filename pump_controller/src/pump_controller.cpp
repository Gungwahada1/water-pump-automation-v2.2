#include "pump_controller.h"
#include "config.h"

void PumpController::begin() {
  // Set the output to OFF before starting networking or other peripherals.
  pinMode(PIN_SSR, OUTPUT);
  digitalWrite(PIN_SSR, SSR_OFF_LEVEL);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
}

void PumpController::setTankSample(const TankSample& sample) {
  tank_ = sample;
}

bool PumpController::isSampleFresh() const {
  return tank_.received && millis() - tank_.receivedAt <= SENSOR_TIMEOUT_MS;
}

uint32_t PumpController::sampleAgeMs() const {
  return tank_.received ? millis() - tank_.receivedAt : 0;
}

uint32_t PumpController::pumpRunMs() const {
  return pumpOn_ && havePumpStart_ ? millis() - pumpStartedAt_ : 0;
}

PumpStatus PumpController::statusSnapshot() const {
  PumpStatus snapshot;
  snapshot.tank = tank_;
  snapshot.mode = mode_;
  snapshot.stopReason = stopReason_;
  snapshot.pumpOn = pumpOn_;
  snapshot.runtimeLockout = runtimeLockout_;
  snapshot.sampleFresh = isSampleFresh();
  snapshot.sampleAgeMs = sampleAgeMs();
  snapshot.pumpRunMs = pumpRunMs();
  return snapshot;
}

void PumpController::setPump(bool on, StopReason reason) {
  if (on && runtimeLockout_) return;
  const bool stateChanged = on != pumpOn_;
  const StopReason previousReason = stopReason_;
  if (on && !pumpOn_) {
    pumpStartedAt_ = millis();
    havePumpStart_ = true;
  }
  if (!on) havePumpStart_ = false;
  pumpOn_ = on;
  stopReason_ = on ? StopReason::NONE : reason;
  digitalWrite(PIN_SSR, on ? SSR_ACTIVE_LEVEL : SSR_OFF_LEVEL);
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);

  if (stateChanged) {
    Serial.printf("[PUMP] %s | SSR GPIO%d=%s (active level %s)\n",
                  on ? "ON" : "OFF", PIN_SSR,
                  on ? "ACTIVE" : "OFF",
                  SSR_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  }
  if (!on && (previousReason != stopReason_ || lastLoggedStopReason_ != reason)) {
    Serial.printf("[CONTROL] stop/idle reason: %s\n", stopReasonName(reason));
    lastLoggedStopReason_ = reason;
  }
}

void PumpController::update(const PhysicalInputs& inputs) {
  if (inputs.mode != mode_) {
    mode_ = inputs.mode;
    autoCycleActive_ = false;
    if (mode_ == PumpMode::AUTO_MODE) manualRequest_ = false;
    Serial.printf("[INPUT] mode selector -> %s\n", modeName(mode_));
  }

  if (inputs.pumpOffPressed) {
    Serial.println("[INPUT] PUMP OFF button pressed");
    manualRequest_ = false;
    autoCycleActive_ = false;
    autoStopInhibit_ = true;
    setPump(false, StopReason::MANUAL_STOP);
  } else if (inputs.pumpOnPressed && mode_ == PumpMode::MANUAL_MODE && !runtimeLockout_) {
    Serial.println("[INPUT] PUMP ON button pressed");
    manualRequest_ = true;
    Serial.println("[CONTROL] manual pump request latched ON");
  } else if (inputs.pumpOnPressed && mode_ != PumpMode::MANUAL_MODE) {
    Serial.println("[INPUT] PUMP ON button pressed");
    Serial.println("[CONTROL] ON button ignored: selector is not MANUAL");
  } else if (inputs.pumpOnPressed && runtimeLockout_) {
    Serial.println("[INPUT] PUMP ON button pressed");
    Serial.println("[CONTROL] ON button ignored: runtime lockout is active");
  }

  if (pumpOn_ && havePumpStart_ && millis() - pumpStartedAt_ >= MAX_PUMP_RUNTIME_MS) {
    Serial.println("[SAFETY] maximum pump runtime reached; locking out until reset");
    runtimeLockout_ = true;
    manualRequest_ = false;
    autoCycleActive_ = false;
    setPump(false, StopReason::RUNTIME_LIMIT);
  }

  // Float-full indication is the highest-priority software stop in both modes.
  const bool floatSaysFull = ENABLE_FLOAT_SWITCH_CUTOFF && tank_.received && tank_.floatFull;
  const bool ultrasonicSaysFull = tank_.received && tank_.valid &&
      tank_.levelPercent >= ULTRASONIC_FULL_STOP_PERCENT;
  if (floatSaysFull || ultrasonicSaysFull) {
    const StopReason fullReason = floatSaysFull
        ? StopReason::FLOAT_FULL : StopReason::ULTRASONIC_FULL;
    if (pumpOn_ || stopReason_ != fullReason) {
      Serial.printf("[SAFETY] full cutoff by %s; stopping pump in any mode\n",
                    floatSaysFull ? "float switch" : "ultrasonic 100%");
    }
    manualRequest_ = false;
    autoCycleActive_ = false;
    setPump(false, fullReason);
    return;
  }

  if (mode_ == PumpMode::MANUAL_MODE) {
    if (manualRequest_ && !runtimeLockout_) setPump(true);
    else if (pumpOn_) setPump(false, StopReason::MANUAL_STOP);
    return;
  }

  if (!tank_.received) {
    autoCycleActive_ = false;
    setPump(false, StopReason::NO_DATA);
    return;
  }
  if (!isSampleFresh()) {
    autoCycleActive_ = false;
    setPump(false, StopReason::STALE_DATA);
    return;
  }
  if (!tank_.valid) {
    autoCycleActive_ = false;
    setPump(false, StopReason::INVALID_SENSOR);
    return;
  }
  if (runtimeLockout_) {
    setPump(false, StopReason::RUNTIME_LIMIT);
    return;
  }

  if (tank_.levelPercent > AUTO_START_LEVEL_PERCENT) autoStopInhibit_ = false;
  if (!autoCycleActive_ && !autoStopInhibit_ &&
      tank_.levelPercent <= AUTO_START_LEVEL_PERCENT) autoCycleActive_ = true;

  if (autoCycleActive_) setPump(true);
  else setPump(false, StopReason::NONE);
}

#include "pump_controller.h"
#include "config.h"
#include "system_log.h"
#include <stdio.h>

void PumpController::begin() {
  // Set the output to OFF before starting networking or other peripherals.
  pinMode(PIN_SSR, OUTPUT);
  digitalWrite(PIN_SSR, SSR_OFF_LEVEL);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
}

void PumpController::forceOffForRestart() {
  manualRequest_ = false;
  dashboardManualRequest_ = false;
  autoCycleActive_ = false;
  setPump(false, StopReason::MANUAL_STOP);
}

void PumpController::setTankSample(const TankSample& sample) {
  tank_ = sample;
}

const char* PumpController::requestDashboardOnReason() const {
  if (mode_ != PumpMode::MANUAL_MODE) return "physical_selector_must_be_manual";
  if (runtimeLockout_) return "runtime_lockout_active";
  if (!tank_.received) return "tank_data_not_received";
  if (!isSampleFresh()) return "tank_data_stale";
  if (!tank_.valid) return "ultrasonic_sensor_invalid";
  if (tank_.levelPercent >= ULTRASONIC_FULL_STOP_PERCENT) return "tank_full_ultrasonic";
  if (ENABLE_FLOAT_SWITCH_CUTOFF && tank_.floatFull) return "tank_full_float";
  return nullptr;
}

bool PumpController::isSampleFresh() const {
  return tank_.received && millis() - tank_.receivedAt <= settings_.tankTimeoutMs;
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
  snapshot.maxPumpRuntimeMs = settings_.maxPumpRuntimeMs;
  snapshot.autoStartLevelPercent = settings_.autoStartPercent;
  snapshot.autoStopLevelPercent = settings_.autoStopPercent;
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
    systemLog().add(on ? "Pump turned ON" : "Pump turned OFF");
  }
  if (!on && (previousReason != stopReason_ || lastLoggedStopReason_ != reason)) {
    Serial.printf("[CONTROL] stop/idle reason: %s\n", stopReasonName(reason));
    char event[72];
    snprintf(event, sizeof(event), "Pump stop/idle reason: %s", stopReasonName(reason));
    systemLog().add(event);
    lastLoggedStopReason_ = reason;
  }
}

void PumpController::update(const PhysicalInputs& inputs) {
  if (inputs.mode != mode_) {
    mode_ = inputs.mode;
    autoCycleActive_ = false;
    if (mode_ == PumpMode::AUTO_MODE) { manualRequest_ = false; dashboardManualRequest_ = false; }
    Serial.printf("[INPUT] mode selector -> %s\n", modeName(mode_));
    systemLog().add(mode_ == PumpMode::AUTO_MODE ? "AUTO mode selected" : "MANUAL mode selected");
  }

  if (inputs.pumpOffPressed || inputs.dashboardOffRequested) {
    Serial.println("[INPUT] PUMP OFF button pressed");
    manualRequest_ = false;
    dashboardManualRequest_ = false;
    autoCycleActive_ = false;
    autoStopInhibit_ = true;
    setPump(false, StopReason::MANUAL_STOP);
  } else if (inputs.dashboardOnRequested) {
    const char* rejection = requestDashboardOnReason();
    if (rejection == nullptr) {
      manualRequest_ = true;
      dashboardManualRequest_ = true;
      Serial.println("[CONTROL] dashboard manual ON accepted");
    } else {
      Serial.printf("[CONTROL] dashboard manual ON rejected: %s\n", rejection);
    }
  } else if (inputs.pumpOnPressed && mode_ == PumpMode::MANUAL_MODE && !runtimeLockout_) {
    Serial.println("[INPUT] PUMP ON button pressed");
    manualRequest_ = true;
    dashboardManualRequest_ = false;
    Serial.println("[CONTROL] manual pump request latched ON");
  } else if (inputs.pumpOnPressed && mode_ != PumpMode::MANUAL_MODE) {
    Serial.println("[INPUT] PUMP ON button pressed");
    Serial.println("[CONTROL] ON button ignored: selector is not MANUAL");
  } else if (inputs.pumpOnPressed && runtimeLockout_) {
    Serial.println("[INPUT] PUMP ON button pressed");
    Serial.println("[CONTROL] ON button ignored: runtime lockout is active");
  }

  if (pumpOn_ && havePumpStart_ && millis() - pumpStartedAt_ >= settings_.maxPumpRuntimeMs) {
    Serial.println("[SAFETY] maximum pump runtime reached; locking out until reset");
    runtimeLockout_ = true;
    manualRequest_ = false;
    dashboardManualRequest_ = false;
    autoCycleActive_ = false;
    setPump(false, StopReason::RUNTIME_LIMIT);
  }

  // Float-full indication is the highest-priority software stop in both modes.
  const bool floatSaysFull = ENABLE_FLOAT_SWITCH_CUTOFF && tank_.received && tank_.floatFull;
  const float ultrasonicStopPercent = mode_ == PumpMode::AUTO_MODE
      ? settings_.autoStopPercent : ULTRASONIC_FULL_STOP_PERCENT;
  const bool ultrasonicSaysFull = tank_.received && tank_.valid &&
      tank_.levelPercent >= ultrasonicStopPercent;
  if (floatSaysFull || ultrasonicSaysFull) {
    const StopReason fullReason = floatSaysFull
        ? StopReason::FLOAT_FULL : StopReason::ULTRASONIC_FULL;
    if (pumpOn_ || stopReason_ != fullReason) {
      Serial.printf("[SAFETY] full cutoff by %s; stopping pump\n",
                    floatSaysFull ? "float switch" : "ultrasonic threshold");
    }
    manualRequest_ = false;
    dashboardManualRequest_ = false;
    autoCycleActive_ = false;
    autoStopInhibit_ = true;
    setPump(false, fullReason);
    return;
  }

  if (mode_ == PumpMode::MANUAL_MODE) {
    if (dashboardManualRequest_ && (!tank_.received || !isSampleFresh() || !tank_.valid)) {
      manualRequest_ = false;
      dashboardManualRequest_ = false;
      setPump(false, !tank_.received ? StopReason::NO_DATA : !isSampleFresh() ? StopReason::STALE_DATA : StopReason::INVALID_SENSOR);
      return;
    }
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

  if (tank_.levelPercent > settings_.autoStartPercent) autoStopInhibit_ = false;
  if (!autoCycleActive_ && !autoStopInhibit_ &&
      tank_.levelPercent <= settings_.autoStartPercent) autoCycleActive_ = true;

  if (autoCycleActive_) setPump(true);
  else setPump(false, StopReason::NONE);
}

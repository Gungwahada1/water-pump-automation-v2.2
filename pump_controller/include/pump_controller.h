#pragma once

#include <Arduino.h>
#include "pump_types.h"

class PumpController {
 public:
  void begin();
  void setTankSample(const TankSample& sample);
  void update(const PhysicalInputs& inputs);

  const TankSample& tankSample() const { return tank_; }
  PumpMode mode() const { return mode_; }
  bool isPumpOn() const { return pumpOn_; }
  bool isRuntimeLockedOut() const { return runtimeLockout_; }
  StopReason stopReason() const { return stopReason_; }
  bool isSampleFresh() const;
  uint32_t sampleAgeMs() const;
  uint32_t pumpRunMs() const;
  PumpStatus statusSnapshot() const;

 private:
  void setPump(bool on, StopReason reason = StopReason::NONE);

  TankSample tank_;
  PumpMode mode_ = PumpMode::AUTO_MODE;
  StopReason stopReason_ = StopReason::NO_DATA;
  bool pumpOn_ = false;
  bool autoCycleActive_ = false;
  bool runtimeLockout_ = false;
  bool manualRequest_ = false;
  bool autoStopInhibit_ = false;
  bool havePumpStart_ = false;
  uint32_t pumpStartedAt_ = 0;
  StopReason lastLoggedStopReason_ = StopReason::NO_DATA;
};

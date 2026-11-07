#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "tank_sensors.h"

class TankClient {
 public:
  void begin();
  void updateWiFi();
  bool isConnected() const;
  int postReading(const TankReading& reading);

 private:
  uint32_t lastWiFiAttemptAt_ = 0;
};

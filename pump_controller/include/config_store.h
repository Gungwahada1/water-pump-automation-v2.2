#pragma once

#include "pump_types.h"

class ConfigStore {
 public:
  static SystemSettings defaults();
  static bool load(SystemSettings& settings);
  static bool save(const SystemSettings& settings);
  static bool validate(const SystemSettings& settings);
};

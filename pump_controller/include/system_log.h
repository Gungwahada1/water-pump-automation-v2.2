#pragma once
#include <Arduino.h>
struct LogEntry { uint32_t uptimeMs = 0; char message[72] = {}; };
class SystemLog {
 public:
  void add(const char* message);
  uint8_t copy(LogEntry* out, uint8_t capacity) const;
 private:
  static constexpr uint8_t CAPACITY = 24;
  LogEntry entries_[CAPACITY];
  uint8_t next_ = 0, count_ = 0;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
SystemLog& systemLog();

#include "system_log.h"
#include <string.h>
SystemLog& systemLog() { static SystemLog instance; return instance; }
void SystemLog::add(const char* message) {
  portENTER_CRITICAL(&mux_);
  LogEntry& entry = entries_[next_]; entry.uptimeMs = millis();
  strncpy(entry.message, message ? message : "event", sizeof(entry.message) - 1);
  entry.message[sizeof(entry.message) - 1] = '\0';
  next_ = (next_ + 1) % CAPACITY; if (count_ < CAPACITY) ++count_;
  portEXIT_CRITICAL(&mux_);
}
uint8_t SystemLog::copy(LogEntry* out, uint8_t capacity) const {
  if (!out || !capacity) return 0;
  portENTER_CRITICAL(&mux_);
  const uint8_t n = count_ < capacity ? count_ : capacity;
  const uint8_t start = (next_ + CAPACITY - count_) % CAPACITY;
  for (uint8_t i = 0; i < n; ++i) out[i] = entries_[(start + i) % CAPACITY];
  portEXIT_CRITICAL(&mux_); return n;
}

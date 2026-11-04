#pragma once
// Minimal Stream / StreamConstPtr of the ESP8266 core: only what PSN-Presepe-WiFi.ino uses.
#include <cstddef>
#include <cstdint>
struct Stream {
  unsigned long timeout = 1000;    // the core's default
  void setTimeout(unsigned long t) { timeout = t; }
  unsigned long getTimeout() const { return timeout; }
  virtual ~Stream() {}
};
struct StreamConstPtr : Stream {
  const char *buf; size_t size;
  StreamConstPtr(const char *b, size_t n) : buf(b), size(n) {}
  StreamConstPtr(const uint8_t *b, size_t n) : buf((const char *)b), size(n) {}
};

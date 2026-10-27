#pragma once
#include "Arduino.h"
struct MDNSMock { std::string host; void begin(const char *h) { host = h; } void addService(const char *, const char *, int) {} void update() {} };
extern MDNSMock MDNS;

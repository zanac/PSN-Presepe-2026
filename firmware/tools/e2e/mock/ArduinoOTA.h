#pragma once
#include "Arduino.h"
struct OTAMock { std::string host, pass; int begins = 0; void setHostname(const char *h) { host = h; } void setPassword(const char *p) { pass = p; } void begin() { begins++; } void handle() {} };
extern OTAMock ArduinoOTA;

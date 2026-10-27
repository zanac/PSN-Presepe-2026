#pragma once
#include "Arduino.h"
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3 };
enum { WIFI_STA = 1 };
struct IPAddressMock { std::string a; String toString() const { return String(a); } };
struct WiFiMock {
  int st = WL_IDLE_STATUS; std::string ip = "0.0.0.0", ssid, pass, host; int begins = 0;
  void persistent(bool) {} void mode(int) {} void disconnect() {} void setAutoReconnect(bool) {}
  void hostname(const String &h) { host = h.s; }
  void begin(const char *s, const char *p) { ssid = s; pass = p ? p : "(open)"; begins++; }
  int status() { return st; }
  IPAddressMock localIP() { return IPAddressMock{ip}; }
  int RSSI() { return -55; }
};
extern WiFiMock WiFi;

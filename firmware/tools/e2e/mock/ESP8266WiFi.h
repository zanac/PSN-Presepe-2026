#pragma once
#include "Arduino.h"
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3 };
enum { WIFI_STA = 1, WIFI_AP = 2 };
struct IPAddress {
  uint8_t b[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(int a, int c, int d, int e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  bool fromString(const String &s) { int x[4]; char z; if (std::sscanf(s.c_str(), "%d.%d.%d.%d%c", &x[0], &x[1], &x[2], &x[3], &z) != 4) return false;
    for (int i = 0; i < 4; i++) { if (x[i] < 0 || x[i] > 255) return false; b[i] = (uint8_t)x[i]; } return true; }
  String toString() const { char t[16]; std::snprintf(t, sizeof t, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]); return String(t); }
};
struct IPAddressMock { std::string a; String toString() const { return String(a); } };
struct WiFiMock {
  int st = WL_IDLE_STATUS; std::string ip = "0.0.0.0", ssid, pass, host; int begins = 0, modo = 0;
  // access point
  int aps = 0, apCanale = 0, apNascosta = 0, apMax = 0, stazioni = 0; std::string apSsid, apPass; IPAddress apIp;
  void persistent(bool) {} void mode(int m) { modo = m; } void disconnect() {} void setAutoReconnect(bool) {}
  void hostname(const String &h) { host = h.s; }
  void begin(const char *s, const char *p) { ssid = s; pass = p ? p : "(open)"; begins++; }
  int status() { return st; }
  IPAddressMock localIP() { return IPAddressMock{ip}; }
  int RSSI() { return -55; }
  bool softAPConfig(IPAddress a, IPAddress, IPAddress) { apIp = a; return true; }
  bool softAP(const char *s, const char *p, int canale, int nascosta, int max) {
    apSsid = s; apPass = p ? p : "(open)"; apCanale = canale; apNascosta = nascosta; apMax = max; aps++; return true;
  }
  IPAddress softAPIP() { return apIp; }
  int softAPgetStationNum() { return stazioni; }
};
extern WiFiMock WiFi;

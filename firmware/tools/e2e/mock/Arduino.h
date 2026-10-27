// Minimal Arduino/ESP8266 core for compiling PSN-Presepe-WiFi.ino on a PC (end-to-end test).
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <functional>
#include <map>
#include <vector>
#include "../wire.h"
#define PROGMEM
inline unsigned long millis() { return T; }
inline void delay(unsigned long ms) { for (unsigned long i = 0; i < ms; i++) { T++; megaStep(); } }
inline void yield() {}

class String {
 public:
  std::string s;
  String() {}
  String(const char *c) : s(c ? c : "") {}
  String(const std::string &x) : s(x) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(double v, unsigned char dec) { char b[64]; std::snprintf(b, sizeof b, "%.*f", dec, v); s = b; }
  size_t length() const { return s.size(); }
  const char *c_str() const { return s.c_str(); }
  char operator[](size_t i) const { return i < s.size() ? s[i] : 0; }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *o) { s += o; return *this; }
  String &operator+=(char c) { s += c; return *this; }
  bool operator==(const String &o) const { return s == o.s; }
  bool operator==(const char *o) const { return s == o; }
  bool operator!=(const String &o) const { return s != o.s; }
  bool startsWith(const char *p) const { return s.compare(0, std::strlen(p), p) == 0; }
  String substring(size_t a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(size_t a, size_t b) const { if (a > s.size()) return String(); if (b > s.size()) b = s.size(); return b <= a ? String() : String(s.substr(a, b - a)); }
  int indexOf(char c, size_t from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  long toInt() const { return std::strtol(s.c_str(), nullptr, 10); }
  void reserve(size_t n) { s.reserve(n); }
  void replace(const char *a, const char *b) { std::string A(a), B(b); size_t p = 0; while ((p = s.find(A, p)) != std::string::npos) { s.replace(p, A.size(), B); p += B.size(); } }
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b - a + 1); }
};
inline String operator+(const String &a, const String &b) { return String(a.s + b.s); }
inline String operator+(const String &a, const char *b) { return String(a.s + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }

// ESP UART0 <-> Mega Serial3
struct EspSerial {
  void setRxBufferSize(size_t) {}
  void begin(unsigned long) {}
  void setDebugOutput(bool) {}
  int available() { return (int)megaToEsp.size(); }
  int read() { if (megaToEsp.empty()) return -1; int c = megaToEsp.front(); megaToEsp.pop_front(); return c; }
  void put(char c) { if (!perso()) espToMega.push_back((unsigned char)c); }
  void print(const String &x) { for (char c : x.s) put(c); }
  void print(const char *x) { while (*x) put(*x++); }
  void print(char c) { put(c); }
};
extern EspSerial Serial;
struct EspMock { int restarts = 0; void restart() { restarts++; } };
extern EspMock ESP;

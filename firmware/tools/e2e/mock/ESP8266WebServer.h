#pragma once
#include "Arduino.h"
enum HTTPMethod { HTTP_GET, HTTP_POST };
enum { BASIC_AUTH };
struct Risposta { int code = 0; std::string type, body; std::map<std::string, std::string> headers; };
struct ESP8266WebServer {
  std::map<std::pair<std::string, int>, std::function<void()>> rotte;
  std::function<void()> nonTrovato;
  std::string body, user, pass;      // current request
  Risposta r; std::map<std::string, std::string> pending;
  ESP8266WebServer(int) {}
  void on(const char *p, HTTPMethod m, std::function<void()> f) { rotte[{p, m}] = f; }
  void onNotFound(std::function<void()> f) { nonTrovato = f; }
  void begin() {} void handleClient() {}
  String arg(const char *n) { return std::strcmp(n, "plain") == 0 ? String(body) : String(); }
  bool authenticate(const char *u, const char *p) { return user == u && pass == p; }
  void requestAuthentication(int, const char *) { r.code = 401; r.headers = pending; pending.clear(); }
  void sendHeader(const char *n, const String &v) { pending[n] = v.s; }
  void send(int c, const char *t, const String &b) { r.code = c; r.type = t; r.body = b.s; r.headers = pending; pending.clear(); }
  void send(int c, const char *t, const char *b) { send(c, t, String(b)); }
  void send_P(int c, const char *t, const char *b, size_t n) { send(c, t, String(std::string(b, n))); }
  Risposta chiama(const char *p, HTTPMethod m, const std::string &b, const char *u = "admin", const char *pw = "presepe2026") {
    body = b; user = u; pass = pw; r = Risposta();
    auto it = rotte.find({p, m});
    if (it == rotte.end()) nonTrovato(); else it->second();
    return r;
  }
};

#pragma once
#include "ESP8266WiFi.h"
enum class DNSReplyCode { NoError = 0 };
struct DNSServer {
  bool attivo = false; std::string dominio; IPAddress ip; long richieste = 0;
  void setErrorReplyCode(DNSReplyCode) {}
  bool start(int, const char *d, IPAddress a) { attivo = true; dominio = d; ip = a; return true; }
  void processNextRequest() { richieste++; }
};

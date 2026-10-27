// Mega side of the end-to-end test: the real CONFIG and LINK WIFI blocks of
// PSN-Presepe.ino (extracted by run_tests.sh) with mocked Serial3, microSD and outputs.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <deque>
#include <random>
#include "wire.h"

struct __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper *>(x))
unsigned long T = 0;
std::deque<int> espToMega, megaToEsp;
double perdita = 0;
long persi = 0;                         // bytes dropped by the loss model
static std::mt19937 rng(12345);
static unsigned long millis() { return T++; }   // internal: the ESP side has its own millis()

#include "cfgblock.h"

struct Printer {
  virtual void put(char c) = 0;
  void print(const __FlashStringHelper *s) { for (const char *p = (const char *)s; *p; p++) put(*p); }
  void print(const char *s) { while (*s) put(*s++); }
  void print(char c) { put(c); }
  void print(unsigned char v) { print(std::to_string((unsigned)v).c_str()); }
  void print(int v) { print(std::to_string(v).c_str()); }
  void print(unsigned v) { print(std::to_string(v).c_str()); }
  void print(long v) { print(std::to_string(v).c_str()); }
  void print(unsigned long v) { print(std::to_string(v).c_str()); }
  void println() { put('\r'); put('\n'); }
  template <class X> void println(X x) { print(x); println(); }
};
struct Debug : Printer { void put(char) override {} };
static Debug Serial;                    // internal: the ESP side has its own Serial
struct Ser3 : Printer {
  void put(char c) override { megaToEsp.push_back((unsigned char)c); }
  void begin(unsigned long) {}
  int available() { return (int)espToMega.size(); }
  int read() { if (espToMega.empty()) return -1; int c = espToMega.front(); espToMega.pop_front(); return c; }
  void flush() {}
};
static Ser3 Serial3;

const int FILE_READ = 0, FILE_WRITE = 1;
std::map<std::string, std::string> disk;
struct File {
  std::string name; size_t pos = 0; int mode = -1;
  explicit operator bool() const { return mode >= 0; }
  int read() { std::string &d = disk[name]; return pos < d.size() ? (unsigned char)d[pos++] : -1; }
  int read(void *buf, size_t n) { std::string &d = disk[name]; size_t k = 0; while (k < n && pos < d.size()) ((uint8_t *)buf)[k++] = (uint8_t)d[pos++]; return (int)k; }
  size_t write(const uint8_t *b, size_t n) { disk[name].append((const char *)b, n); return n; }
  void close() { mode = -1; }
};
struct SDMock {
  bool begin(int) { return true; } void end() {}
  bool exists(const char *n) { return disk.count(n) != 0; }
  bool remove(const char *n) { return disk.erase(n) != 0; }
  File open(const char *n, int mode) { File f; if (mode == FILE_READ && !disk.count(n)) return f; if (mode == FILE_WRITE) disk[n]; f.name = n; f.mode = mode; return f; }
} SD;
const uint8_t PIN_SD_CS = 53;
struct Adafruit_NeoPixel {
  uint32_t px[100] = {};
  void clear() { std::memset(px, 0, sizeof px); } void setPixelColor(uint16_t i, uint32_t c) { if (i < 100) px[i] = c; } void show() {}
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
} stelle, casette;
#define NUM_STELLE (cfg.numStelle)
#define NUM_CASETTE (cfg.numCasette)
enum Fase { GIORNO, TRAMONTO, CREPUSCOLO, NOTTE, ALBA };
char wifiIp[16] = "none";
bool testInCorso = false, testWeb = false, testMostraIP = false, modalitaColore = false, running = true;
uint8_t testIndice = 0;
uint16_t releStato = 0;
uint8_t rgbCieloR, rgbCieloG, rgbCieloB, rgbTramontoR, rgbTramontoG, rgbTramontoB, rgbAlbaR, rgbAlbaG, rgbAlbaB;
unsigned long durataCiclo() { return 60000UL; }
float percentualeCiclo(unsigned long) { return 45.0f; }
Fase faseDaPercentuale(float) { return TRAMONTO; }
float percentualeFase(float, Fase) { return 50.0f; }
void setCielo(uint8_t r, uint8_t g, uint8_t b) { rgbCieloR = r; rgbCieloG = g; rgbCieloB = b; }
void setTramonto(uint8_t r, uint8_t g, uint8_t b) { rgbTramontoR = r; rgbTramontoG = g; rgbTramontoB = b; }
void setAlba(uint8_t r, uint8_t g, uint8_t b) { rgbAlbaR = r; rgbAlbaG = g; rgbAlbaB = b; }
void scriviRele(uint8_t i, bool on) { if (on) releStato |= (uint16_t)(1U << i); else releStato &= (uint16_t)~(1U << i); }
void spegniRele() { for (uint8_t i = 0; i < 16; i++) scriviRele(i, false); }
void tuttoSpento() { setCielo(0, 0, 0); setTramonto(0, 0, 0); setAlba(0, 0, 0); }
void buzzerBeep(bool = true) {}
void faseAvanti() {}
void mostraOledTest() {}
void toggleStartStop() { if (testInCorso) { testInCorso = testWeb = false; spegniRele(); return; } running = !running; }
void entraTestWeb() { if (!testInCorso) testInCorso = true; testWeb = true; tuttoSpento(); spegniRele(); }

#include "link.inc"

// Loss model: a WS2811 refresh keeps interrupts off for ~1.5 ms (~17 bytes at 115200).
// A command that arrives while the Mega is idle (not in its quiet window) loses, with
// probability 'perdita', a run of 1..17 bytes at its start; then the Mega receives
// bytes again and stays quiet (no refresh) while the transfer goes on.
bool perso() {
  static bool deciso = false; static int tratto = 0;
  if (perdita <= 0 || lnkQuiet()) { deciso = false; return false; }
  if (!deciso) { deciso = true; tratto = std::uniform_real_distribution<double>(0, 1)(rng) < perdita ? 1 + (int)(rng() % 17) : 0; }
  if (tratto > 0) { tratto--; persi++; return true; }
  return false;
}

bool megaPresente = true;
void megaStep() { if (megaPresente) lnkTick(); }
// --- accessors for the test
void megaAvvia(const char *ssid, const char *pwd) {
  cfgDefault(cfg); cfg.wifiAbilitato = 1;
  std::strcpy(cfg.wifiSsid, ssid); std::strcpy(cfg.wifiPassword, pwd); std::strcpy(cfg.nome[2], "Mulino");
  lnkInizio();
}
std::string &megaFile(const char *n) { return disk[n]; }
bool megaHaFile(const char *n) { return disk.count(n) != 0; }
uint16_t megaRele() { return releStato; }
const char *megaIp() { return wifiIp; }
bool megaRiavvio() { return lnkRiavvio; }
bool megaTestWeb() { return testWeb; }

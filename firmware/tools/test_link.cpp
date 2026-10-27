// Host-side test of the WiFi link (Mega side), no Arduino needed.
// Run with firmware/tools/run_tests.sh, which extracts from PSN-Presepe.ino:
//   cfgblock.h = the CONFIG BEGIN/END block
//   link.inc   = the LINK WIFI BEGIN/END block
// The ESP8266 (Serial3), the microSD (in-memory files), the WS2811 chains and the
// rest of the sketch (outputs, test mode, cycle) are mocked below. The test plays the
// ESP side of the protocol: config push, IP, status, web test commands, file read,
// file write with retransmissions, commit/backup/restore, errors and timeouts.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <map>

struct __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper *>(x))
unsigned long T = 0;
unsigned long millis() { return T++; }   // the clock moves a little at every reading

#include "cfgblock.h"

// ------------------------------------------------------------------ Print mock
struct Printer {
  std::string out;
  void print(const __FlashStringHelper *s) { out += (const char *)s; }
  void print(const char *s) { out += s; }
  void print(char c) { out += c; }
  void print(unsigned char v) { out += std::to_string((unsigned)v); }
  void print(int v) { out += std::to_string(v); }
  void print(unsigned v) { out += std::to_string(v); }
  void print(long v) { out += std::to_string(v); }
  void print(unsigned long v) { out += std::to_string(v); }
  void println() { out += "\r\n"; }
  template <class X> void println(X x) { print(x); println(); }
};
struct Serial3Mock : Printer {
  std::deque<int> in;
  bool begun = false;
  void begin(unsigned long) { begun = true; }
  int available() { return (int)in.size(); }
  int read() { if (in.empty()) return -1; int c = in.front(); in.pop_front(); return c; }
  void flush() {}
} Serial3;
Printer Serial;

// ------------------------------------------------------------------ microSD mock
const int FILE_READ = 0, FILE_WRITE = 1;
std::map<std::string, std::string> disk;
bool cardOk = true, writeFails = false;
int writesLeft = -1;      // >= 0: the next writes succeed this many times, then fail (power loss / bad card)
struct File {
  std::string name; size_t pos = 0; int mode = -1;
  explicit operator bool() const { return mode >= 0; }
  int read() { std::string &d = disk[name]; return pos < d.size() ? (unsigned char)d[pos++] : -1; }
  int read(void *buf, size_t n) {
    std::string &d = disk[name]; size_t k = 0;
    while (k < n && pos < d.size()) ((uint8_t *)buf)[k++] = (uint8_t)d[pos++];
    return (int)k;
  }
  size_t write(const uint8_t *b, size_t n) {
    if (writeFails || writesLeft == 0) return 0;
    if (writesLeft > 0) writesLeft--;
    disk[name].append((const char *)b, n); return n;
  }
  size_t print(const char *s) { return write((const uint8_t *)s, std::strlen(s)); }
  void close() { mode = -1; }
};
struct SDMock {
  int begins = 0, ends = 0;
  bool begin(int) { begins++; return cardOk; }
  void end() { ends++; }
  bool exists(const char *n) { return disk.count(n) != 0; }
  bool remove(const char *n) { return disk.erase(n) != 0; }
  File open(const char *n, int mode) {
    File f;
    if (mode == FILE_READ && !disk.count(n)) return f;
    if (mode == FILE_WRITE) disk[n];       // create, append
    f.name = n; f.mode = mode; return f;
  }
} SD;
const uint8_t PIN_SD_CS = 53;
const int INPUT_PULLUP = 2; int pullup15 = 0;
void pinMode(int pin, int mode) { if (pin == 15 && mode == INPUT_PULLUP) pullup15++; }

// ------------------------------------------------------------------ WS2811 mock
struct Adafruit_NeoPixel {
  uint32_t px[100] = {}; int shows = 0;
  void clear() { std::memset(px, 0, sizeof px); }
  void setPixelColor(uint16_t i, uint32_t c) { if (i < 100) px[i] = c; }
  void show() { shows++; }
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
} stelle, casette;
#define NUM_STELLE (cfg.numStelle)
#define NUM_CASETTE (cfg.numCasette)

// ------------------------------------------------------------------ rest of the sketch
enum Fase { GIORNO, TRAMONTO, CREPUSCOLO, NOTTE, ALBA };
char wifiIp[16] = "none";
bool wifiAp = false;
bool testInCorso = false, testWeb = false, testMostraIP = false, modalitaColore = false, running = true;
uint8_t testIndice = 0;
uint16_t releStato = 0;
uint8_t rgbCieloR, rgbCieloG, rgbCieloB, rgbTramontoR, rgbTramontoG, rgbTramontoB, rgbAlbaR, rgbAlbaG, rgbAlbaB;
int beeps = 0, avanti = 0, oledTest = 0;
unsigned long durataCiclo() { return 60000UL; }
float percentualeCiclo(unsigned long) { return 45.0f; }
Fase faseDaPercentuale(float) { return TRAMONTO; }
float percentualeFase(float, Fase) { return 50.0f; }
void setCielo(uint8_t r, uint8_t g, uint8_t b) { rgbCieloR = r; rgbCieloG = g; rgbCieloB = b; }
void setTramonto(uint8_t r, uint8_t g, uint8_t b) { rgbTramontoR = r; rgbTramontoG = g; rgbTramontoB = b; }
void setAlba(uint8_t r, uint8_t g, uint8_t b) { rgbAlbaR = r; rgbAlbaG = g; rgbAlbaB = b; }
void scriviRele(uint8_t i, bool on) { if (on) releStato |= (uint16_t)(1U << i); else releStato &= (uint16_t)~(1U << i); }
void spegniRele() { for (uint8_t i = 0; i < 16; i++) scriviRele(i, false); }
void tuttoSpento() { setCielo(0, 0, 0); setTramonto(0, 0, 0); setAlba(0, 0, 0); stelle.clear(); casette.clear(); }
void buzzerBeep(bool = true) { beeps++; }
void faseAvanti() { avanti++; }
void mostraOledTest() { oledTest++; }
void toggleStartStop() {
  if (testInCorso) { testInCorso = testWeb = testMostraIP = false; tuttoSpento(); spegniRele(); running = true; return; }
  running = !running;
}
void entraTestWeb() {   // same as the sketch
  if (!testInCorso) { testInCorso = true; testIndice = 0; running = false; }
  testWeb = true; testMostraIP = false; tuttoSpento(); spegniRele(); mostraOledTest();
}

#include "link.inc"

// ------------------------------------------------------------------ ESP side
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static std::vector<std::string> lines(const std::string &s, bool keepStatus = false) {
  std::vector<std::string> v; std::string cur;
  for (char ch : s) {
    if (ch == '\r') continue;
    if (ch == '\n') { if (keepStatus || cur.compare(0, 4, "@ST ") != 0) v.push_back(cur); cur.clear(); }
    else cur += ch;
  }
  return v;
}
static void sendRaw(const std::string &s) { for (char ch : s) Serial3.in.push_back((unsigned char)ch); }
static std::string withSum(const std::string &l) {
  char h[4]; std::snprintf(h, sizeof h, "%02X", lnkSomma(l.c_str(), l.size()));
  return l + "*" + h;
}
// Sends one command, runs the Mega once, returns its reply lines (status and @ACK excluded).
static int acks = 0;
static std::vector<std::string> cmd(const std::string &l) {
  Serial3.out.clear();
  sendRaw(withSum(l) + "\n");
  lnkTick();
  std::vector<std::string> v, r = lines(Serial3.out);
  for (auto &x : r) { if (x == "@ACK") acks++; else v.push_back(x); }
  return v;
}
static bool ok(const std::vector<std::string> &r) { return r.size() == 1 && r[0].compare(0, 3, "@OK") == 0; }
static std::string field(const std::string &st, int n) {   // n-th space-separated field of a line
  size_t p = 0;
  for (int i = 0; i < n; i++) { p = st.find(' ', p); if (p == std::string::npos) return ""; p++; }
  return st.substr(p, st.find(' ', p) - p);
}
// GET: rebuilds the text the way the ESP does
static std::string getFile(std::string *name = 0, std::string *end = 0) {
  std::vector<std::string> r = cmd("@GET");
  std::string text;
  for (auto &l : r) {
    if (l.compare(0, 6, "@FILE ") == 0) { if (name) *name = l.substr(6); }
    else if (l.compare(0, 3, "@L ") == 0) text += l.substr(3) + "\n";
    else if (l.compare(0, 3, "@C ") == 0) text += l.substr(3);
    else if (l.compare(0, 5, "@END ") == 0) { if (end) *end = l.substr(5); }
    else text += "<" + l + ">";
  }
  return text;
}

int main() {
  cfgDefault(cfg);

  // 1. [WIFI] abilitato = 0: Serial3 never opened, nothing sent, nothing read
  lnkInizio();
  CHECK(!lnkAttivo && !Serial3.begun && Serial3.out.empty());
  sendRaw(withSum("@T") + "\n"); lnkTick();
  CHECK(Serial3.out.empty() && !testInCorso && Serial3.in.size() > 0 && !lnkQuiet());
  Serial3.in.clear();

  // 2. enabled: configuration pushed at start (SSID, passwords, host, 16 relay names, GO)
  cfg.wifiAbilitato = 1;
  std::strcpy(cfg.wifiSsid, "Casa Rossi"); std::strcpy(cfg.wifiPassword, "ab;c#d=e f");
  std::strcpy(cfg.nome[0], "Mulino");
  lnkInizio();
  {
    std::vector<std::string> v = lines(Serial3.out);
    CHECK(Serial3.begun && v.size() == 27 && pullup15 == 1);
    CHECK(v[0] == "@SSID Casa Rossi" && v[1] == "@WPWD ab;c#d=e f" && v[2] == "@USER admin");
    CHECK(v[3] == "@APWD presepe2026" && v[4] == "@HOST presepe");
    CHECK(v[5] == "@MODE 1" && v[6] == "@APSSID Presepe" && v[7] == "@APPWD presepe2026");
    CHECK(v[8] == "@APOPT 6 0 4 0 60" && v[9] == "@APIP 192.168.4.1");
    CHECK(v[10] == "@NREL 1 Mulino" && v[11] == "@NREL 2 " && v[25] == "@NREL 16 " && v[26] == "@GO");
  }
  // HELLO (ESP rebooted) -> configuration again; ESP boot noise and bad sums are ignored
  CHECK(cmd("@HELLO").size() == 27);
  Serial3.out.clear(); sendRaw("\x7f\x13 ets Jan  8 2013,rst cause:2\n"); lnkTick(); CHECK(Serial3.out.empty());
  Serial3.out.clear(); sendRaw("@HELLO*00\n"); lnkTick(); CHECK(Serial3.out.empty());
  CHECK(lnkQuiet()); T += 200; CHECK(!lnkQuiet());                       // WS2811 paused while receiving

  // 3. IP address from the ESP
  CHECK(ok(cmd("@IP 192.168.1.23")) && std::strcmp(wifiIp, "192.168.1.23") == 0);
  CHECK(cmd("@IP 1234567890123456").size() == 1 && std::strcmp(wifiIp, "192.168.1.23") == 0);   // too long
  CHECK(ok(cmd("@IP 192.168.4.1 AP")) && std::strcmp(wifiIp, "192.168.4.1") == 0 && wifiAp);   // access point
  CHECK(ok(cmd("@IP 192.168.1.23")) && !wifiAp);
  CHECK(ok(cmd("@IP none")) && std::strcmp(wifiIp, "none") == 0 && !wifiAp);

  // 4. status line once per second: 23 fields
  T += 1000; Serial3.out.clear(); lnkTick();
  {
    std::vector<std::string> v = lines(Serial3.out, true);
    CHECK(v.size() == 1 && v[0].compare(0, 4, "@ST ") == 0);
    CHECK(field(v[0], 1) == "0" && field(v[0], 2) == "1" && field(v[0], 3) == "1" && field(v[0], 4) == "50");
    CHECK(field(v[0], 5) == "450" && field(v[0], 6) == "60" && field(v[0], 10) == "50" && field(v[0], 22) == "1");
    CHECK(field(v[0], 23) == "");
  }

  // 5. web test: strips, relays, pixels, colour, beep, errors, exit
  CHECK(ok(cmd("@T")) && testInCorso && testWeb);
  CHECK(ok(cmd("@S 0 10 20 30")) && rgbCieloR == 10 && rgbCieloG == 20 && rgbCieloB == 30);
  CHECK(ok(cmd("@S 2 255 0 1")) && rgbAlbaR == 255 && rgbAlbaB == 1);
  CHECK(cmd("@S 3 1 1 1")[0] == "@ERR arg" && cmd("@S 0 1 1")[0] == "@ERR arg" && cmd("@S 0 1 1 256")[0] == "@ERR arg");
  CHECK(ok(cmd("@R 3 1")) && releStato == 4);
  CHECK(ok(cmd("@R 16 1")) && releStato == 0x8004 && cmd("@R 17 1")[0] == "@ERR arg");
  CHECK(ok(cmd("@R 3 0")) && releStato == 0x8000);
  CHECK(ok(cmd("@P 0 5 1")) && stelle.px[5] == Adafruit_NeoPixel::Color(70, 50, 27) && stelle.px[4] == 0);
  CHECK(cmd("@P 0 50 1")[0] == "@ERR arg");                               // 50 stars: 0..49
  CHECK(ok(cmd("@PA 1 1")) && casette.px[0] != 0 && casette.px[49] != 0 && casette.px[50] == 0);
  CHECK(ok(cmd("@PC 1 2 3")) && stelle.px[5] == Adafruit_NeoPixel::Color(1, 2, 3) && casette.px[7] == Adafruit_NeoPixel::Color(1, 2, 3));
  CHECK(ok(cmd("@P 1 7 0")) && casette.px[7] == 0 && casette.px[8] != 0);
  int b0 = beeps; CHECK(ok(cmd("@B")) && beeps == b0 + 1);
  CHECK(cmd("@RUN")[0] == "@ERR test" && cmd("@NEXT")[0] == "@ERR test");
  T += 1000; Serial3.out.clear(); lnkTick();
  CHECK(field(lines(Serial3.out, true)[0], 1) == "2" && field(lines(Serial3.out, true)[0], 12) == "32768");
  CHECK(ok(cmd("@X")) && !testInCorso && !testWeb && releStato == 0);
  CHECK(ok(cmd("@X")));                                                    // harmless outside the test
  // a relay command from the normal cycle enters the web test by itself, with all outputs off
  CHECK(ok(cmd("@R 1 1")) && testWeb && releStato == 1 && stelle.px[5] == 0);
  CHECK(ok(cmd("@X")));
  CHECK(ok(cmd("@RUN")) && !running && ok(cmd("@RUN")) && running);
  int a0 = avanti; CHECK(ok(cmd("@NEXT")) && avanti == a0 + 1);
  // numbered: a retransmission (same number) gets the same reply and is not executed again
  a0 = avanti;
  CHECK(ok(cmd("@NEXT 41")) && ok(cmd("@NEXT 41")) && ok(cmd("@NEXT 41")) && avanti == a0 + 1);
  CHECK(ok(cmd("@NEXT 42")) && avanti == a0 + 2);
  CHECK(ok(cmd("@RUN 43")) && !running && ok(cmd("@RUN 43")) && !running && ok(cmd("@RUN 44")) && running);
  CHECK(ok(cmd("@T")) && cmd("@NEXT 45")[0] == "@ERR test" && cmd("@NEXT 45")[0] == "@ERR test" && ok(cmd("@X")));
  modalitaColore = true;
  CHECK(cmd("@S 0 1 1 1")[0] == "@ERR modo colore" && cmd("@T")[0] == "@ERR modo colore" && !testInCorso);
  modalitaColore = false;
  CHECK(cmd("@ZZZ")[0] == "@ERR comando");

  // 6. GET: CRLF removed, long lines split in 120-char pieces, no trailing newline
  std::string lunga(250, 'x');
  disk["PRESEPE.INI"] = "[CICLO]\r\ndurata1 = 60 ; uno\r\n\r\n" + lunga + "\nultima";
  {
    std::string name, end;
    std::string t = getFile(&name, &end);
    CHECK(name == "PRESEPE.INI" && t == "[CICLO]\ndurata1 = 60 ; uno\n\n" + lunga + "\nultima\n");
    CHECK(end == "5 " + std::to_string(disk["PRESEPE.INI"].size()));
  }

  // 7. PUT: retransmitted pieces are written once, leading spaces kept, empty lines kept
  std::string vecchio = disk["PRESEPE.INI"];
  CHECK(ok(cmd("@PUT")) && disk.count("PRESEPE.NEW"));
  CHECK(ok(cmd("@W 0 [WIFI]")) && ok(cmd("@W 0 [WIFI]")));                 // second one = retransmission
  CHECK(ok(cmd("@C 1 ssid = Casa")) && ok(cmd("@W 2  Rossi")));            // "Casa" + " Rossi"
  CHECK(ok(cmd("@W 3 ")) && ok(cmd("@W 4    rientro")));
  std::string nuovo = "[WIFI]\nssid = Casa Rossi\n\n   rientro\n";
  CHECK(disk["PRESEPE.NEW"] == nuovo);
  CHECK(cmd("@COMMIT " + std::to_string(nuovo.size()))[0] == "@ERR arg");            // the number is required
  CHECK(lnkScrittura);
  CHECK(cmd("@COMMIT " + std::to_string(nuovo.size() + 1) + " 100")[0] == "@ERR lunghezza");   // wrong length: nothing changes
  CHECK(cmd("@COMMIT " + std::to_string(nuovo.size() + 1) + " 100")[0] == "@ERR lunghezza");   // same reply when retransmitted
  CHECK(disk["PRESEPE.INI"] == vecchio && !disk.count("PRESEPE.NEW") && !disk.count("PRESEPE.BAK"));
  CHECK(cmd("@W 5 x")[0] == "@ERR put");                                   // writing needs PUT first
  CHECK(ok(cmd("@PUT")) && ok(cmd("@W 0 [WIFI]")) && ok(cmd("@W 1 ssid = Casa Rossi")) && ok(cmd("@W 2 ")) && ok(cmd("@W 3    rientro")));
  {
    int a0 = acks;
    std::vector<std::string> r = cmd("@COMMIT " + std::to_string(nuovo.size()) + " 101");
    CHECK(r.size() == 1 && r[0] == "@OK PRESEPE.INI" && acks == a0 + 1);
    r = cmd("@COMMIT " + std::to_string(nuovo.size()) + " 101");          // reply lost: same answer, not "put"
    CHECK(r.size() == 1 && r[0] == "@OK PRESEPE.INI" && acks == a0 + 1);
    CHECK(cmd("@COMMIT " + std::to_string(nuovo.size()) + " 102")[0] == "@ERR put");
  }
  CHECK(disk["PRESEPE.INI"] == nuovo && disk["PRESEPE.BAK"] == vecchio && !disk.count("PRESEPE.NEW"));
  CHECK(getFile() == nuovo);

  // 8. RESTORE swaps the file with its backup (a second RESTORE undoes it)
  CHECK(cmd("@RESTORE")[0] == "@ERR arg");
  CHECK(ok(cmd("@RESTORE 200")) && disk["PRESEPE.INI"] == vecchio && disk["PRESEPE.BAK"] == nuovo && !disk.count("PRESEPE.NEW"));
  CHECK(ok(cmd("@RESTORE 200")) && disk["PRESEPE.INI"] == vecchio && disk["PRESEPE.BAK"] == nuovo);   // retransmission: no second swap
  CHECK(ok(cmd("@RESTORE 201")) && disk["PRESEPE.INI"] == nuovo && disk["PRESEPE.BAK"] == vecchio);
  CHECK(!disk.count("PRESEPE.NEW") && !disk.count("PRESEPE.CMT"));
  disk.erase("PRESEPE.BAK");
  CHECK(cmd("@RESTORE 202")[0] == "@ERR nessun backup" && disk["PRESEPE.INI"] == nuovo);

  // 8b. empty upload is refused: the old file stays
  CHECK(ok(cmd("@PUT")) && cmd("@COMMIT 0 203")[0] == "@ERR vuoto" && disk["PRESEPE.INI"] == nuovo && !disk.count("PRESEPE.NEW"));

  // 8c. copy .NEW -> file fails half way (bad card): the previous file is put back from .BAK
  {
    CHECK(ok(cmd("@PUT")) && ok(cmd("@W 0 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
    writesLeft = 3;           // .BAK copy (1 block of 64), the mark, the first 64 of the 90 bytes; then the card fails
    std::vector<std::string> r = cmd("@COMMIT 90 204");
    writesLeft = -1;
    CHECK(r.size() == 1 && r[0] == "@ERR copia");
    // putting .BAK back fails too (card still failing): the mark stays, the next boot finishes the copy
    CHECK(disk.count("PRESEPE.CMT") && disk.count("PRESEPE.NEW"));
    CHECK(lnkRecupera() && disk["PRESEPE.INI"].size() == 90 && !disk.count("PRESEPE.CMT") && !disk.count("PRESEPE.NEW"));
    CHECK(!lnkRecupera());                                                  // nothing left to do
  }
  // 8d. power lost during the copy: at boot the copy is done again from .NEW
  disk["PRESEPE.INI"] = "[CICLO]\ndura";                                  // truncated
  disk["PRESEPE.NEW"] = "[CICLO]\ndurata1 = 60\n";
  disk["PRESEPE.CMT"] = "PRESEPE.INI";
  CHECK(lnkRecupera() && disk["PRESEPE.INI"] == "[CICLO]\ndurata1 = 60\n" && !disk.count("PRESEPE.CMT") && !disk.count("PRESEPE.NEW"));
  disk["PRESEPE.CMT"] = "PRESEPE.INI";                                     // mark without .NEW: removed, file untouched
  CHECK(!lnkRecupera() && !disk.count("PRESEPE.CMT") && disk["PRESEPE.INI"] == "[CICLO]\ndurata1 = 60\n");
  disk["PRESEPE.INI"] = nuovo; disk["PRESEPE.BAK"] = vecchio;

  // 9. only PRESEPE.TXT on the card (Wokwi): that file is read and written
  disk.clear(); disk["PRESEPE.TXT"] = "a\n";
  {
    std::string name; CHECK(getFile(&name) == "a\n" && name == "PRESEPE.TXT");
    CHECK(ok(cmd("@PUT")) && ok(cmd("@W 0 b")));
    std::vector<std::string> r = cmd("@COMMIT 2 300");
    CHECK(r.size() == 1 && r[0] == "@OK PRESEPE.TXT" && disk["PRESEPE.TXT"] == "b\n" && disk["PRESEPE.BAK"] == "a\n");
    CHECK(!disk.count("PRESEPE.INI"));
  }
  // empty card: GET returns an empty file named PRESEPE.INI, COMMIT creates it without backup
  disk.clear();
  {
    std::string name, end; CHECK(getFile(&name, &end) == "" && name == "PRESEPE.INI" && end == "0 0");
    CHECK(ok(cmd("@PUT")) && ok(cmd("@W 0 x")) && ok(cmd("@COMMIT 2 301")) && disk["PRESEPE.INI"] == "x\n" && !disk.count("PRESEPE.BAK"));
  }

  // 10. errors: no card, write failure, interrupted upload (30 s), too long line
  cardOk = false;
  CHECK(cmd("@GET")[0] == "@ERR sd" && cmd("@PUT")[0] == "@ERR sd" && cmd("@RESTORE 302")[0] == "@ERR sd");
  cardOk = true;
  CHECK(ok(cmd("@PUT")));
  writeFails = true; CHECK(cmd("@W 0 x")[0] == "@ERR sd" && !lnkScrittura && !disk.count("PRESEPE.NEW")); writeFails = false;
  CHECK(ok(cmd("@PUT")) && ok(cmd("@W 0 x")) && lnkScrittura);
  T += 31000; lnkTick(); CHECK(!lnkScrittura && !disk.count("PRESEPE.NEW") && disk["PRESEPE.INI"] == "x\n");
  CHECK(cmd("@GET").size() >= 2);                                          // card usable again
  Serial3.out.clear(); sendRaw(withSum("@W 0 " + std::string(210, 'y')) + "\n@B"); lnkTick();
  CHECK(Serial3.out.empty());                                              // > 200 chars: dropped, no reply
  sendRaw(std::string("*") + "\n"); lnkTick();
  CHECK(ok(cmd("@B")));                                                    // next line works

  // 11. REBOOT: reply first, restart on the next loop
  CHECK(ok(cmd("@REBOOT")) && lnkRiavvio);

  std::printf("link test: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

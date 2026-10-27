// ESP8266 side of the end-to-end test: the real PSN-Presepe-WiFi.ino compiled on a PC
// with mocked WiFi/web server, talking to the real Mega link code (mega_side.cpp)
// through a serial line that loses bytes in the ESP -> Mega direction.
#include "mock/Arduino.h"
#include "mock/ESP8266WiFi.h"
#include "mock/ESP8266WebServer.h"
#include "mock/ESP8266mDNS.h"
#include "mock/ArduinoOTA.h"
EspSerial Serial;
EspMock ESP;
WiFiMock WiFi;
MDNSMock MDNS;
OTAMock ArduinoOTA;
#include ESP_INO

void megaAvvia(const char *ssid, const char *pwd);
std::string &megaFile(const char *n);
bool megaHaFile(const char *n);
uint16_t megaRele();
const char *megaIp();
bool megaRiavvio();
bool megaTestWeb();
extern bool megaPresente;
extern int megaAvanti;
extern std::map<std::string, std::string> disk;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static void gira(unsigned long ms) { for (unsigned long i = 0; i < ms; i++) { T++; megaStep(); loop(); } }

int main() {
  setup();
  // 1. boot: the Mega sends SSID and password; the ESP joins the network and reports its IP
  megaFile("PRESEPE.INI") = "[CICLO]\r\ndurata1 = 60\r\n";
  megaAvvia("Casa;Rossi", "ab;c#d=e f");
  gira(50);
  CHECK(configOk && WiFi.begins == 1 && WiFi.ssid == "Casa;Rossi" && WiFi.pass == "ab;c#d=e f" && WiFi.host == "presepe");
  CHECK(nomiRele[2] == "Mulino" && cfgUtente == "admin" && cfgApwd == "presepe2026");
  WiFi.st = WL_CONNECTED; WiFi.ip = "192.168.1.50";
  gira(4000);
  CHECK(std::strcmp(megaIp(), "192.168.1.50") == 0 && servizi && MDNS.host == "presepe" && ArduinoOTA.pass == "presepe2026");

  // 2. authentication
  CHECK(server.chiama("/", HTTP_GET, "", "admin", "sbagliata").code == 401);
  Risposta r = server.chiama("/", HTTP_GET, "");
  CHECK(r.code == 200 && r.headers["Content-Encoding"] == "gzip" && r.body.size() == PAGINA_PANNELLO_LEN);

  // 3. status JSON
  gira(1200);
  r = server.chiama("/api/stato", HTTP_GET, "");
  CHECK(r.code == 200 && r.body.find("\"mega\":true") != std::string::npos && r.body.find("\"pct\":45.0") != std::string::npos);
  CHECK(r.body.find("\"nomi\":[\"\",\"\",\"Mulino\"") != std::string::npos && r.body.find("\"ip\":\"192.168.1.50\"") != std::string::npos);

  perdita = 0.5;                                  // from now on: 50 % of the commands sent to an idle Mega lose their first bytes

  // 4. web test commands
  r = server.chiama("/api/cmd", HTTP_POST, "R 3 1");
  CHECK(r.code == 200 && r.body.find("\"ok\":true") != std::string::npos && megaRele() == 4 && megaTestWeb());
  CHECK(server.chiama("/api/cmd", HTTP_POST, "r 3 1").code == 400);            // lower case refused by the ESP
  CHECK(server.chiama("/api/cmd", HTTP_POST, "GET").code == 400);              // file commands not via /api/cmd
  r = server.chiama("/api/cmd", HTTP_POST, "R 17 1");
  CHECK(r.code == 200 && r.body.find("\"ok\":false") != std::string::npos && r.body.find("arg") != std::string::npos);

  // 4b. requests without the pages' X-Presepe header (another site in the same browser) are refused
  CHECK(server.chiama("/api/cmd", HTTP_POST, "R 3 0", "admin", "presepe2026", {}).code == 403 && megaRele() == 4);
  CHECK(server.chiama("/api/ini", HTTP_POST, "x\n", "admin", "presepe2026", {}).code == 403);
  CHECK(server.chiama("/api/riavvia", HTTP_POST, "", "admin", "presepe2026", {}).code == 403 && !megaRiavvio());
  r = server.chiama("/api/cmd", HTTP_POST, "X");                                // leave the test
  // 4c. NEXT with losses: executed exactly once per click that got an answer
  {
    int a0 = megaAvanti, okn = 0;
    for (int i = 0; i < 10; i++) { r = server.chiama("/api/cmd", HTTP_POST, "NEXT"); if (r.body.find("\"ok\":true") != std::string::npos) okn++; }
    CHECK(okn == 10 && megaAvanti == a0 + 10);
  }

  // 5. read the file (CRLF removed)
  r = server.chiama("/api/ini", HTTP_GET, "");
  CHECK(r.code == 200 && r.body == "[CICLO]\ndurata1 = 60\n" && r.headers["X-File"] == "PRESEPE.INI");

  // 6. write: long lines, UTF-8, empty lines, CRLF, leading spaces, no final newline; 20 rounds with losses
  std::string lunga(250, 'z'), testo;
  for (int i = 0; i < 40; i++) testo += "; riga " + std::to_string(i) + " citt\xc3\xa0 \xe2\x80\x94 prova\r\n";
  testo += lunga + "\n\n   rientro;#\"x\"*41\n[WIFI]\nssid = \"Casa;Rossi\"\nultima senza a capo";
  std::string atteso = testo; for (size_t p; (p = atteso.find('\r')) != std::string::npos;) atteso.erase(p, 1);
  atteso += "\n";
  bool tuttiOk = true;
  for (int giro = 0; giro < 20; giro++) {
    std::string vecchio = megaFile("PRESEPE.INI");
    std::string t = testo + std::to_string(giro);
    r = server.chiama("/api/ini", HTTP_POST, t);
    std::string a = atteso.substr(0, atteso.size() - 1) + std::to_string(giro) + "\n";
    bool ok = r.code == 200 && r.body.find("\"file\":\"PRESEPE.INI\"") != std::string::npos &&
              megaFile("PRESEPE.INI") == a && megaFile("PRESEPE.BAK") == vecchio && !megaHaFile("PRESEPE.NEW");
    if (!ok) { tuttiOk = false; std::printf("round %d: code %d body %s\n", giro, r.code, r.body.c_str()); }
  }
  CHECK(tuttiOk);
  r = server.chiama("/api/ini", HTTP_GET, "");
  CHECK(r.code == 200 && r.body == atteso.substr(0, atteso.size() - 1) + "19\n");

  // 6b. empty or truncated body (ESP out of memory): refused, nothing sent to the Mega
  {
    std::string prima = megaFile("PRESEPE.INI");
    r = server.chiama("/api/ini", HTTP_POST, "");
    CHECK(r.code == 400 && r.body.find("vuoto") != std::string::npos && megaFile("PRESEPE.INI") == prima);
    r = server.chiama("/api/ini", HTTP_POST, "[CICLO]\n", "admin", "presepe2026", {{"X-Presepe", "1"}, {"X-Len", "12000"}});
    CHECK(r.code == 400 && megaFile("PRESEPE.INI") == prima && !megaHaFile("PRESEPE.NEW"));
  }

  // 7. restore swaps with the backup
  std::string ora = megaFile("PRESEPE.INI"), prima = megaFile("PRESEPE.BAK");
  r = server.chiama("/api/ripristina", HTTP_POST, "");
  CHECK(r.code == 200 && megaFile("PRESEPE.INI") == prima && megaFile("PRESEPE.BAK") == ora);
  CHECK(!megaHaFile("PRESEPE.NEW") && !megaHaFile("PRESEPE.CMT"));

  // 8. reboot request reaches the Mega
  r = server.chiama("/api/riavvia", HTTP_POST, "");
  CHECK(r.code == 200 && megaRiavvio());

  // 9. Mega silent (power off / DIP switches): clear message, no hang
  megaPresente = false; perdita = 0;
  unsigned long t0 = T;
  r = server.chiama("/api/cmd", HTTP_POST, "B");
  CHECK(r.body.find("il presepe non risponde") != std::string::npos && T - t0 < 3000);
  megaPresente = true;

  // 10. new network settings from the Mega after the start: clean ESP restart
  megaAvvia("AltraRete", "12345678");
  gira(50);
  CHECK(ESP.restarts == 1);

  // 11. incomplete configuration (a line lost while the ESP was starting): not applied
  {
    String ssid0 = cfgSsid; int r0 = ESP.restarts;
    rigaDalMega("@SSID Terza"); rigaDalMega("@WPWD 12345678"); rigaDalMega("@APWD x"); rigaDalMega("@HOST h"); rigaDalMega("@GO");
    CHECK(cfgSsid == ssid0 && ESP.restarts == r0);                             // @USER missing
  }

  CHECK(persi > 50);                              // the loss model really dropped bytes
  std::printf("end-to-end test: %d checks, %d failures (%ld bytes lost and recovered)\n", checks, fails, persi);
  return fails ? 1 : 0;
}

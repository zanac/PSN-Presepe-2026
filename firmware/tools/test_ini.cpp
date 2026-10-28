// Host-side test of the default PRESEPE.INI written to an empty microSD, no Arduino needed.
// Run with firmware/tools/run_tests.sh, which extracts from PSN-Presepe.ino:
//   cfgblock.h  = the CONFIG BEGIN/END block (parser, defaults)
//   iniprede.inc = the INI PREDEFINITO BEGIN/END block (text embedded by embed_ini.py)
//   iniscrivi.inc = the INI SCRIVI BEGIN/END block (iniCreaPredefinito)
// argv[1] = firmware/PSN-Presepe/PRESEPE.INI
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include "cfgblock.h"

#define PROGMEM
static uint8_t pgm_read_byte(const char *p) { return (uint8_t)*p; }

const int FILE_READ = 0, FILE_WRITE = 1;
std::map<std::string, std::string> disk;
bool cardOk = true; int writesLeft = -1;
struct File {
  std::string name; int mode = -1;
  explicit operator bool() const { return mode >= 0; }
  size_t write(const uint8_t *b, size_t n) {
    if (writesLeft == 0) return 0;
    if (writesLeft > 0) writesLeft--;
    disk[name].append((const char *)b, n); return n;
  }
  void close() { mode = -1; }
};
struct SDMock {
  int begins = 0, ends = 0;
  bool begin(int) { begins++; return cardOk; }
  void end() { ends++; }
  bool exists(const char *n) { return disk.count(n) != 0; }
  bool remove(const char *n) { return disk.erase(n) != 0; }
  File open(const char *n, int mode) { File f; if (mode == FILE_WRITE) disk[n]; f.name = n; f.mode = mode; return f; }
} SD;
const uint8_t PIN_SD_CS = 53;

#include "iniprede.inc"
#include "iniscrivi.inc"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(int argc, char **argv) {
  std::ifstream in(argc > 1 ? argv[1] : "PRESEPE.INI", std::ios::binary);
  std::stringstream ss; ss << in.rdbuf();
  std::string repo = ss.str();

  // 1. empty card: PRESEPE.INI written with CR LF, repo file + header, parses with 0 errors
  CHECK(iniCreaPredefinito() && disk.count("PRESEPE.INI") && SD.begins == SD.ends);
  std::string w = disk["PRESEPE.INI"], lf;
  bool crlf = true;                                                        // every line ends with CR LF
  for (size_t i = 0; i < w.size(); i++) {
    if (w[i] == '\r') { crlf = crlf && i + 1 < w.size() && w[i + 1] == '\n'; continue; }
    if (w[i] == '\n') crlf = crlf && i > 0 && w[i - 1] == '\r';
    lf += w[i];
  }
  CHECK(crlf);
  CHECK(lf.size() > repo.size() && lf.compare(lf.size() - repo.size(), repo.size(), repo) == 0);   // ends with the repo file
  CHECK(lf.compare(0, 2, "; ") == 0 && lf.find("l'ha scritto la centralina") != std::string::npos);
  CHECK(std::strlen(INI_PREDEFINITO) == lf.size());
  {
    PresepeConfig a, d; cfgDefault(a); cfgDefault(d);
    std::string buf = w; cfgParseBuffer(a, &buf[0]);
    CHECK(a.errori == 0);
    CHECK(std::memcmp(&a.durataMs, &d.durataMs, sizeof d.durataMs) == 0 && a.numStelle == d.numStelle && a.numEventi == d.numEventi);
  }
  // 2. a file is already there (PRESEPE.INI or PRESEPE.TXT): nothing written
  CHECK(!iniCreaPredefinito() && disk["PRESEPE.INI"] == w);
  disk.clear(); disk["PRESEPE.TXT"] = "x";
  CHECK(!iniCreaPredefinito() && !disk.count("PRESEPE.INI") && disk["PRESEPE.TXT"] == "x");
  // 3. no card
  disk.clear(); cardOk = false;
  CHECK(!iniCreaPredefinito() && disk.empty());
  cardOk = true;
  // 4. card full / write-protected half way: no partial file left
  writesLeft = 10;
  CHECK(!iniCreaPredefinito() && !disk.count("PRESEPE.INI"));
  writesLeft = 0;
  CHECK(!iniCreaPredefinito() && !disk.count("PRESEPE.INI"));
  writesLeft = -1;

  std::printf("default INI test: %d checks, %d failures (%zu bytes written)\n", checks, fails, w.size());
  return fails ? 1 : 0;
}

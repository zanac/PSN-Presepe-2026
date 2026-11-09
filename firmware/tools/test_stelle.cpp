// Star engine of PSN-Presepe.ino (random stars and [COSTELLAZIONI]) on the PC.
//   cfgblock.h = the CONFIG BEGIN/END block
//   stelle.inc = from "struct StatoStelle {" to riempiCatena (generaCieloStellato, mostraStelle, ...)
// Checks: constellation stars never among the random ones, fewer random stars when few are
// free, level outside the cycle (star level) and in it (accendi / spegni, also past the end
// of the cycle), luminosity and colour, modes insieme / una_alla_volta / scintilla.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "cfgblock.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)


static unsigned long orologio = 0;
unsigned long millis() { return orologio; }
unsigned long micros() { return orologio * 1000UL; }
long random(long a) { return a > 0 ? std::rand() % a : 0; }
long random(long a, long b) { return b > a ? a + std::rand() % (b - a) : a; }
template <class T> T constrain(T v, T a, T b) { return v < a ? a : v > b ? b : v; }
bool lnkQuiet() { return false; }
struct Adafruit_NeoPixel {
  uint32_t px[100] = {};
  void clear() { std::memset(px, 0, sizeof px); }
  void setPixelColor(uint16_t i, uint32_t c) { if (i < 100) px[i] = c; }
  void show() {}
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
} stelle01, stelle02;
Adafruit_NeoPixel *const catena[2] = { &stelle01, &stelle02 };
enum Fase { GIORNO, TRAMONTO, CREPUSCOLO, NOTTE, ALBA };
void stelleFlush();
#include "stelle.inc"

static int accese(const Adafruit_NeoPixel &n, int numero) { int k = 0; for (int i = 0; i < numero; i++) if (n.px[i]) k++; return k; }

static uint32_t grigio(int v) { return Adafruit_NeoPixel::Color(v, v, v); }
static int rosso(uint32_t c) { return (int)(c >> 16) & 255; }

int main() {
  cfgDefault(cfg);
  cfgParseBuffer(cfg,
    "[STELLE_01]\nnumero = 50\nattive = 20\ncolore = 100,100,100\nlivello_notte = 200\n"
    "[STELLE_02]\nnumero = 10\nattive = 8\ncolore = 100,50,0\n"
    "[COSTELLAZIONE]\nstelle = 01:1-5, 02:1\nluminosita = 100\n"
    "[COSTELLAZIONE]\nstelle = 01:50, 02:2-4\nluminosita = 40\n");
  CHECK(cfg.errori == 0 && cfg.numCostellazioni == 2);

  for (int notte = 0; notte < 200; notte++) {          // many random skies: never a constellation star
    generaTuttiICieli();
    CHECK(statoStelle[0].liberi == 44 && statoStelle[1].liberi == 6);
    for (int k = 0; k < 2; k++)
      for (int p = 0; p < statoStelle[k].liberi; p++) CHECK(!cfgCost(cfg, k, statoStelle[k].ordine[p]));
  }

  // outside the cycle (posStelle < 0): follow the star level; full night = livello_notte
  orologio = 0;
  mostraStelle(0, 200 / 255.0f);
  for (int i = 0; i < 5; i++) CHECK(stelle01.px[i] == grigio(100));
  CHECK(stelle01.px[49] == grigio(40));
  CHECK(accese(stelle01, 50) <= 6 + 20 && accese(stelle01, 50) >= 6);   // random ones may be dark while twinkling
  mostraStelle(1, 235 / 255.0f);
  CHECK(stelle02.px[0] == Adafruit_NeoPixel::Color(100, 50, 0) && stelle02.px[1] == Adafruit_NeoPixel::Color(40, 20, 0));
  CHECK(accese(stelle02, 10) <= 4 + 6);                // 8 "attive" but only 6 free stars
  mostraStelle(0, 100 / 255.0f);
  for (int i = 0; i < 5; i++) CHECK(stelle01.px[i] == grigio(50));
  mostraStelle(0, 0.0f); mostraStelle(1, 0.0f);
  CHECK(accese(stelle01, 50) == 0 && accese(stelle02, 10) == 0);

  // in the cycle: default times = up during the whole dusk, down during the whole dawn
  const float CR = cfg.pCrepuscolo, NO = cfg.pNotte, AL = cfg.pAlba;
  posStelle = (CR + NO) / 2; mostraStelle(0, 0.0f);          // level of the random stars does not matter
  CHECK(stelle01.px[0] == grigio(50) && stelle01.px[49] == grigio(20));
  posStelle = (NO + AL) / 2; mostraStelle(0, 0.0f);
  CHECK(stelle01.px[0] == grigio(100));
  posStelle = 99.99f; mostraStelle(0, 0.0f); CHECK(rosso(stelle01.px[0]) <= 1);
  posStelle = 10.0f;  mostraStelle(0, 0.0f); CHECK(stelle01.px[0] == 0);

  // own times, colour, modes
  cfgDefault(cfg);
  cfgParseBuffer(cfg,
    "[STELLE_01]\nnumero = 20\nattive = 0\ncolore = 100,100,100\n"
    "[STELLE_02]\nnumero = 20\nattive = 0\ncolore = 100,100,100\n"
    "[COSTELLAZIONE]\nstelle = 01:1-2, 02:1-2\nluminosita = 200\nmodo = una_alla_volta\n"
    "accendi = CREPUSCOLO 0 - CREPUSCOLO 100\nspegni = ALBA 0 - ALBA 100\n"
    "[COSTELLAZIONE]\nstelle = 01:10\nluminosita = 100\ncolore = 100,0,50\n"
    "accendi = NOTTE 50 - NOTTE 50\nspegni = GIORNO 10 - GIORNO 20\n"         // goes past the end of the cycle
    "[COSTELLAZIONE]\nstelle = 01:12\nluminosita = 200\nmodo = scintilla\n"
    "accendi = TRAMONTO 0 - TRAMONTO 0\nspegni = ALBA 100 - ALBA 100\n");
  CHECK(cfg.errori == 0 && cfg.numCostellazioni == 3 && cfg.cost[0].n == 4 && cfg.cost[0].n0 == 2);
  generaTuttiICieli();
  auto pos = [](int f, float pct) { return cfgPosizione(cfg, (uint8_t)f, pct); };
  // one at a time: 4 stars, at a quarter of the way up only the first is (almost) on
  posStelle = pos(CFG_CREPUSCOLO, 30); mostraStelle(0, 0.0f); mostraStelle(1, 0.0f);
  { float t = 0.3f, m = t * t * (3 - 2 * t), v1 = m * 4;     // level 0.216 -> 0.864 of the first star
    CHECK(rosso(stelle01.px[0]) == (int)(200 * (v1 > 1 ? 1 : v1) + 0.5f) && stelle01.px[1] == 0 && stelle02.px[0] == 0); }
  posStelle = pos(CFG_CREPUSCOLO, 60); mostraStelle(0, 0.0f); mostraStelle(1, 0.0f);
  CHECK(stelle01.px[0] == grigio(200) && stelle01.px[1] == grigio(200) && rosso(stelle02.px[0]) > 0 && stelle02.px[1] == 0);
  posStelle = pos(CFG_NOTTE, 10); mostraStelle(0, 0.0f); mostraStelle(1, 0.0f);
  CHECK(stelle01.px[0] == grigio(200) && stelle02.px[1] == grigio(200));
  posStelle = pos(CFG_ALBA, 60); mostraStelle(0, 0.0f); mostraStelle(1, 0.0f);   // going down: the last ones go first
  CHECK(stelle01.px[0] == grigio(200) && stelle02.px[1] == 0);
  // own colour; from the middle of the night past the end of the cycle to 20 % of the day
  posStelle = pos(CFG_NOTTE, 40); mostraStelle(0, 0.0f); CHECK(stelle01.px[9] == 0);
  posStelle = pos(CFG_NOTTE, 60); mostraStelle(0, 0.0f); CHECK(stelle01.px[9] == Adafruit_NeoPixel::Color(100, 0, 50));
  posStelle = pos(CFG_GIORNO, 5);  mostraStelle(0, 0.0f); CHECK(stelle01.px[9] == Adafruit_NeoPixel::Color(100, 0, 50));
  posStelle = pos(CFG_GIORNO, 15); mostraStelle(0, 0.0f); CHECK(rosso(stelle01.px[9]) > 0 && rosso(stelle01.px[9]) < 100);
  posStelle = pos(CFG_GIORNO, 30); mostraStelle(0, 0.0f); CHECK(stelle01.px[9] == 0);
  // twinkle: over 20 s the star goes full, dark and in between
  bool pieno = false, buio = false, mezzo = false;
  posStelle = pos(CFG_NOTTE, 50);
  for (orologio = 0; orologio < 20000; orologio += 100) {
    mostraStelle(0, 0.0f); int r = rosso(stelle01.px[11]);
    pieno |= r == 200; buio |= r == 0; mezzo |= r > 0 && r < 200;
  }
  CHECK(pieno && buio && mezzo);
  posStelle = -1.0f;

  // without constellations: as before, the random stars use all the pixels
  cfgDefault(cfg);
  generaTuttiICieli();
  CHECK(statoStelle[0].liberi == 50 && statoStelle[1].liberi == 50);

  std::printf("star engine test: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

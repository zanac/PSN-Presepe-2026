// Star engine of PSN-Presepe.ino (random stars and [COSTELLAZIONI]) on the PC.
//   cfgblock.h = the CONFIG BEGIN/END block
//   stelle.inc = from "struct StatoStelle {" to riempiCatena (generaCieloStellato, mostraStelle, ...)
// Checks: constellation stars never among the random ones, all constellation stars at the
// same brightness following the star level (0 at the start of the dusk, full at night),
// brightness per constellation, colour of the chain, fewer random stars when few are free.
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

int main() {
  cfgDefault(cfg);
  cfgParseBuffer(cfg,
    "[STELLE_01]\nnumero = 50\nattive = 20\ncolore = 100,100,100\nlivello_notte = 200\n"
    "[STELLE_02]\nnumero = 10\nattive = 8\ncolore = 100,50,0\n"
    "[COSTELLAZIONI]\n"
    "costellazione = 100 | 01:1-5, 02:1\n"
    "costellazione = 40 | 01:50, 02:2-4\n");
  CHECK(cfg.errori == 0 && cfg.numCostellazioni == 2);

  for (int notte = 0; notte < 200; notte++) {          // many random skies: never a constellation star
    generaTuttiICieli();
    CHECK(statoStelle[0].liberi == 44 && statoStelle[1].liberi == 6);
    for (int k = 0; k < 2; k++)
      for (int p = 0; p < statoStelle[k].liberi; p++) CHECK(!cfg.costStella[k][statoStelle[k].ordine[p]]);
  }

  // full night: level = livello_notte; constellation stars at their own brightness and chain colour
  orologio = 0;
  mostraStelle(0, 200 / 255.0f);
  for (int i = 0; i < 5; i++) CHECK(stelle01.px[i] == Adafruit_NeoPixel::Color(100, 100, 100));
  CHECK(stelle01.px[49] == Adafruit_NeoPixel::Color(40, 40, 40));
  CHECK(accese(stelle01, 50) <= 6 + 20 && accese(stelle01, 50) >= 6);   // random ones may be dark while twinkling
  mostraStelle(1, 235 / 255.0f);
  CHECK(stelle02.px[0] == Adafruit_NeoPixel::Color(100, 50, 0) && stelle02.px[1] == Adafruit_NeoPixel::Color(40, 20, 0));
  CHECK(accese(stelle02, 10) <= 4 + 6);                // 8 "attive" but only 6 free stars

  // half way up the dusk: all constellation stars together at half brightness
  mostraStelle(0, 100 / 255.0f);
  for (int i = 0; i < 5; i++) CHECK(stelle01.px[i] == Adafruit_NeoPixel::Color(50, 50, 50));
  CHECK(stelle01.px[49] == Adafruit_NeoPixel::Color(20, 20, 20));
  // level 0 (day, start of the dusk, end of the dawn): all dark
  mostraStelle(0, 0.0f); mostraStelle(1, 0.0f);
  CHECK(accese(stelle01, 50) == 0 && accese(stelle02, 10) == 0);

  // without constellations: as before, the random stars use all the pixels
  cfgDefault(cfg);
  generaTuttiICieli();
  CHECK(statoStelle[0].liberi == 50 && statoStelle[1].liberi == 50);

  std::printf("star engine test: %d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

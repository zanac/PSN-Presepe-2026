// Host-side tests for the PRESEPE.INI reader (no Arduino needed).
// Run with firmware/tools/run_tests.sh: it extracts the CONFIG BEGIN/END block of
// PSN-Presepe.ino into a temporary PresepeConfig.h and compiles this file with it.
#include "PresepeConfig.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <fstream>
#include <sstream>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool sameScenography(const PresepeConfig &a, const PresepeConfig &b) {
  PresepeConfig x = a, y = b;   // ignore read-status bookkeeping
  x.sdStato = y.sdStato = 0; x.righeLette = y.righeLette = 0; x.errori = y.errori = 0;
  x.primaRigaErrata = y.primaRigaErrata = 0; x.eventiDaFile = y.eventiDaFile = 0;
  return std::memcmp(&x, &y, sizeof(x)) == 0;
}

int main(int argc, char **argv) {
  PresepeConfig d; cfgDefault(d);

  // 1. empty text -> defaults, no errors
  { PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, ""); CHECK(c.errori == 0); CHECK(sameScenography(c, d)); }

  // 2. the shipped example file equals the defaults with zero errors
  if (argc > 1) {
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, ss.str().c_str());
    std::printf("example file: %u lines, %u errors (first %u)\n", c.righeLette, c.errori, c.primaRigaErrata);
    CHECK(c.errori == 0); CHECK(sameScenography(c, d)); CHECK(c.eventiDaFile == 1); CHECK(c.numEventi == 2);
  }

  // 3. custom values, CRLF, case, names, Grp_ syntax, forcing, wrap-around casette
  {
    const char *t =
      "[ciclo]\r\nDURATA1 = 30\r\ndurata2=120 ; two minutes\r\ndurata3 = 600\r\npot_min=10\r\npot_max = 680\r\n"
      "pot_invertito = no\r\npartenza = PAUSA\r\n"
      "[FASI]\ntramonto = 35.5\ncrepuscolo=48\nnotte=60\nalba=90\n"
      "[RELE]\nlogica = bassa\nnome1 = Mulino\nnome16 = Fontana\nforza2 = on\nforza3 = off\n"
      "evento = giorno, Mulino, on, 10\nevento = NOTTE, Grp_04_04, ON, 0\nevento = ALBA, 16, spento, 100\n"
      "[COLORI]\ngiorno_caldo = 200, 80, 20\nlum_alba = 50\ngamma = off\n"
      "[STELLE]\nnumero = 80\nattive = 30\ncolore = 100,100,100\n"
      "[CASETTE]\nnumero = 12\naccendi = NOTTE, 90\nspegni = GIORNO, 10\nfuoco = 0\n"
      "[SISTEMA]\nbuzzer = 0\ndebug_ms = 0\n";
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, t);
    CHECK(c.errori == 0);
    CHECK(c.durataMs[0] == 30000UL && c.durataMs[1] == 120000UL && c.durataMs[2] == 600000UL);
    CHECK(c.potMin == 10 && c.potMax == 680 && c.potInvertito == 0 && c.partenzaInPausa == 1);
    CHECK(std::fabs(c.pTramonto - 35.5f) < 1e-4 && c.pAlba == 90.0f);
    CHECK(c.releAttivoBasso == 1 && std::strcmp(c.nome[0], "Mulino") == 0 && std::strcmp(c.nome[15], "Fontana") == 0);
    CHECK(c.forza[1] == FORZA_ON && c.forza[2] == FORZA_OFF && c.forza[0] == FORZA_AUTO);
    CHECK(c.numEventi == 3 && c.eventiDaFile == 1);
    CHECK(c.eventi[0].fase == CFG_GIORNO && c.eventi[0].rele == 0 && c.eventi[0].acceso == 1 && c.eventi[0].pct == 10);
    CHECK(c.eventi[1].fase == CFG_NOTTE && c.eventi[1].rele == 15 && c.eventi[1].acceso == 1);
    CHECK(c.eventi[2].fase == CFG_ALBA && c.eventi[2].rele == 15 && c.eventi[2].acceso == 0 && c.eventi[2].pct == 100);
    CHECK(c.giornoCaldo.r == 200 && c.giornoCaldo.g == 80 && c.giornoCaldo.b == 20 && c.lumAlba == 50 && c.gamma == 0);
    CHECK(c.numStelle == 80 && c.stelleAttive == 30 && c.coloreStelle.g == 100);
    CHECK(c.numCasette == 12 && c.accendiFase == CFG_NOTTE && c.accendiPct == 90 && c.spegniFase == CFG_GIORNO && c.fuoco == 0);
    CHECK(c.buzzer == 0 && c.debugMs == 0);
    CHECK(c.lumCielo == 100 && c.tramontoArancio.r == 155);   // untouched keys keep defaults
  }

  // 4. errors are counted, first bad line reported, good lines still applied
  {
    const char *t =
      "durata1 = 30\n"            // 1: key outside any section
      "[CICLO]\n"                 // 2
      "durata1 = 5\n"             // 3: below minimum (10)
      "durata2 = abc\n"           // 4: not a number
      "durata3 = 400\n"           // 5: ok
      "[NONESISTE]\n"             // 6: unknown section
      "foo = 1\n"                 // 7: no valid section active
      "[RELE]\n"                  // 8
      "evento = NOTTE, 17, ON, 5\n"      // 9: relay out of range
      "evento = NOTTE, Grp_05_01, ON, 5\n" // 10: bad group
      "evento = MEZZOGIORNO, 1, ON, 5\n"   // 11: bad phase
      "evento = NOTTE, 1, FORSE, 5\n"      // 12: bad state
      "evento = NOTTE, 1, ON\n"            // 13: missing field
      "nome1 = NomeTroppoLungo\n"          // 14: > 12 chars
      "[COLORI]\n"                         // 15
      "alba_arancio = 300,0,0\n"           // 16: > 255
      "senza uguale\n";                    // 17
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, t);
    std::printf("error case: %u errors, first line %u\n", c.errori, c.primaRigaErrata);
    CHECK(c.errori == 13); CHECK(c.primaRigaErrata == 1);
    CHECK(c.durataMs[0] == d.durataMs[0] && c.durataMs[1] == d.durataMs[1] && c.durataMs[2] == 400000UL);
    CHECK(c.numEventi == 2 && c.eventiDaFile == 0);   // no valid event -> default table kept
    CHECK(c.albaArancio.r == d.albaArancio.r && c.nome[0][0] == 0);
  }

  // 5. non-increasing phases revert all four; inconsistent ranges revert
  {
    PresepeConfig c; cfgDefault(c);
    cfgParseBuffer(c, "[FASI]\ntramonto=60\ncrepuscolo=50\n[CICLO]\npot_min=500\npot_max=510\n[STELLE]\nnumero=10\nattive=40\nlum_min=90\nlum_max=20\n");
    CHECK(c.pTramonto == d.pTramonto && c.pCrepuscolo == d.pCrepuscolo);
    CHECK(c.potMin == d.potMin && c.potMax == d.potMax);
    CHECK(c.stelleAttive == 10);                       // clamped to the pixel count
    CHECK(c.lumStelleMin == d.lumStelleMin && c.lumStelleMax == d.lumStelleMax);
    CHECK(c.errori == 3 && c.primaRigaErrata == 0xFFFF);
  }

  // 6. event table capacity (64) and over-long line
  {
    std::string t = "[RELE]\n";
    for (int i = 0; i < 70; i++) t += "evento = NOTTE, 1, ON, 1\n";
    t += std::string(200, 'x') + "\n";
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, t.c_str());
    CHECK(c.numEventi == CFG_MAX_EVENTI); CHECK(c.errori == 7);
  }

  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

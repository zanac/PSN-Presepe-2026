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
  for (int s = 0; s < CFG_NUM_STRISCE; s++) x.tappeDaFile[s] = y.tappeDaFile[s] = 0;
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
    CHECK(c.tappeDaFile[CFG_S_CIELO] == 1 && c.tappeDaFile[CFG_S_TRAMONTO] == 1 && c.tappeDaFile[CFG_S_ALBA] == 1);
  }

  // 3. custom values, CRLF, case, names, Grp_ syntax, forcing, wrap-around casette
  {
    const char *t =
      "[ciclo]\r\nDURATA1 = 30\r\ndurata2=120 ; two minutes\r\ndurata3 = 600\r\npot_min=10\r\npot_max = 680\r\n"
      "pot_invertito = no\r\npartenza = PAUSA\r\n"
      "[FASI]\ntramonto = 35.5\ncrepuscolo=48\nnotte=60\nalba=90\n"
      "[RELE]\nlogica = bassa\nnome1 = Mulino\nnome16 = Fontana\nforza2 = on\nforza3 = off\n"
      "evento = giorno, Mulino, on, 10\nevento = NOTTE, Grp_04_04, ON, 0\nevento = ALBA, 16, spento, 100\n"
      "[COLORI]\nlum_alba = 50\ngamma = off\n"
      "[TRAMONTO]\ntappa = tramonto, 10, 200, 80, 20\ntappa = TRAMONTO, 60.5, 0, 0, 255, LINEARE\n"
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
    CHECK(c.lumAlba == 50 && c.gamma == 0);
    CHECK(c.numTappe[CFG_S_TRAMONTO] == 2 && c.tappeDaFile[CFG_S_TRAMONTO] == 1);
    CHECK(c.tappe[CFG_S_TRAMONTO][0].col.r == 200 && c.tappe[CFG_S_TRAMONTO][0].curva == CURVA_MORBIDA);
    CHECK(std::fabs(c.tappe[CFG_S_TRAMONTO][1].pct - 60.5f) < 1e-4 && c.tappe[CFG_S_TRAMONTO][1].curva == CURVA_LINEARE);
    CHECK(c.numTappe[CFG_S_CIELO] == d.numTappe[CFG_S_CIELO] && c.tappeDaFile[CFG_S_CIELO] == 0);   // untouched strips keep defaults
    CHECK(c.numStelle == 80 && c.stelleAttive == 30 && c.coloreStelle.g == 100);
    CHECK(c.numCasette == 12 && c.accendiFase == CFG_NOTTE && c.accendiPct == 90 && c.spegniFase == CFG_GIORNO && c.fuoco == 0);
    CHECK(c.buzzer == 0 && c.debugMs == 0);
    CHECK(c.lumCielo == 100);   // untouched keys keep defaults
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
      "giorno_caldo = 1,2,3\n"             // 16: old key, removed (now [CIELO] tappa)
      "senza uguale\n";                    // 17
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, t);
    std::printf("error case: %u errors, first line %u\n", c.errori, c.primaRigaErrata);
    CHECK(c.errori == 13); CHECK(c.primaRigaErrata == 1);
    CHECK(c.durataMs[0] == d.durataMs[0] && c.durataMs[1] == d.durataMs[1] && c.durataMs[2] == 400000UL);
    CHECK(c.numEventi == 2 && c.eventiDaFile == 0);   // no valid event -> default table kept
    CHECK(c.nome[0][0] == 0);
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

  // 7. colour tappe: interpolation, step, wrap-around, linear curve, errors
  {
    PresepeConfig c; cfgDefault(c);
    cfgParseBuffer(c,
      "[CIELO]\n"
      "tappa = GIORNO, 0, 0, 0, 0\n"            //  1
      "tappa = GIORNO, 50, 200, 100, 0, lineare\n"  //  2  linear 0 -> 200 over the first half of GIORNO
      "tappa = TRAMONTO, 0, 200, 100, 0\n"      //  3
      "tappa = TRAMONTO, 0, 10, 10, 10\n"       //  4  same point = step
      "tappa = GIORNO, 10, 1, 1, 1\n"           //  5  ERROR: goes back in time
      "tappa = NOTTE, 100, 1, 1, 1, veloce\n"   //  6  ERROR: unknown curve
      "tappa = NOTTE, 120, 1, 1, 1\n"           //  7  ERROR: % > 100
      "tappa = NOTTE, 50, 1, 1\n"               //  8  ERROR: missing blue
      "tappa = NOTTE, 50, 1, 1, 1, lineare, x\n" // 9  ERROR: too many fields
      "colore = 1, 2, 3\n"                       // 10  ERROR: unknown key
      "[ALBA]\n"
      "tappa = ALBA, 50, 100, 0, 0\n");          // single tappa = fixed colour all cycle
    CHECK(c.errori == 6); CHECK(c.numTappe[CFG_S_CIELO] == 4); CHECK(c.numTappe[CFG_S_ALBA] == 1);
    // GIORNO = 0..40 % of the cycle: at 10 % (= 25 % of GIORNO, half way to the 50 % tappa) linear gives 100
    CfgRGB a = cfgColoreTappe(c, CFG_S_CIELO, 10.0f);
    CHECK(a.r == 100 && a.g == 50 && a.b == 0);
    CfgRGB b = cfgColoreTappe(c, CFG_S_CIELO, 30.0f);             // between 50 % GIORNO and TRAMONTO 0: constant
    CHECK(b.r == 200 && b.g == 100);
    CfgRGB s1 = cfgColoreTappe(c, CFG_S_CIELO, 40.0f);            // exactly at the step: the later tappa wins
    CHECK(s1.r == 10 && s1.g == 10 && s1.b == 10);
    // wrap-around: from TRAMONTO 0 (40 %) the next tappa is GIORNO 0 of the next cycle (100 %), curve morbida
    CfgRGB w = cfgColoreTappe(c, CFG_S_CIELO, 70.0f);             // x = 0.5 -> smoothstep 0.5 -> 10 -> 0 gives 5
    CHECK(w.r == 5 && w.g == 5 && w.b == 5);
    CfgRGB z = cfgColoreTappe(c, CFG_S_CIELO, 0.0f);
    CHECK(z.r == 0 && z.g == 0 && z.b == 0);
    CfgRGB f1 = cfgColoreTappe(c, CFG_S_ALBA, 3.0f), f2 = cfgColoreTappe(c, CFG_S_ALBA, 95.0f);
    CHECK(f1.r == 100 && f2.r == 100 && f1.g == 0);
    CfgRGB t0 = cfgColoreTappe(c, CFG_S_TRAMONTO, 45.0f), td = cfgColoreTappe(d, CFG_S_TRAMONTO, 45.0f);
    CHECK(t0.r == td.r && t0.g == td.g && t0.b == td.b);           // TRAMONTO not in the file: defaults
    // too many tappe: the 21st is an error
    std::string t = "[TRAMONTO]\n";
    for (int i = 0; i <= CFG_MAX_TAPPE; i++) t += "tappa = TRAMONTO, " + std::to_string(i) + ", 1, 2, 3\n";
    PresepeConfig e; cfgDefault(e); cfgParseBuffer(e, t.c_str());
    CHECK(e.numTappe[CFG_S_TRAMONTO] == CFG_MAX_TAPPE && e.errori == 1);
  }
  // 8. default tappe: the historical key colours are where they used to be
  {
    CfgRGB g = cfgColoreTappe(d, CFG_S_CIELO, 0.0f), wh = cfgColoreTappe(d, CFG_S_CIELO, 40.0f * 0.50f);
    CHECK(g.r == 210 && g.g == 82 && g.b == 18); CHECK(wh.r == 255 && wh.g == 255 && wh.b == 255);   // noon plateau 45..55 %
    CfgRGB q = cfgColoreTappe(d, CFG_S_CIELO, cfgPosizione(d, CFG_GIORNO, 30.0f));
    CHECK(q.r == 246 && q.g == 220 && q.b == 208);
    CfgRGB pk = cfgColoreTappe(d, CFG_S_TRAMONTO, cfgPosizione(d, CFG_TRAMONTO, 38.0f));
    CHECK(pk.r == 155 && pk.g == 92 && pk.b == 16);
    CfgRGB off = cfgColoreTappe(d, CFG_S_ALBA, 20.0f), on = cfgColoreTappe(d, CFG_S_ALBA, 85.0f);
    CHECK(off.r == 0 && off.g == 0 && off.b == 0); CHECK(on.r == 18 && on.g == 12 && on.b == 5);
  }

  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

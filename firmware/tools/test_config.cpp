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
    // bit = phase: CIELO in GIORNO, TRAMONTO, ALBA; tramonto strip in TRAMONTO; alba strip in ALBA
    CHECK(c.tappeDaFile[CFG_S_CIELO] == ((1 << CFG_GIORNO) | (1 << CFG_TRAMONTO) | (1 << CFG_ALBA)));
    CHECK(c.tappeDaFile[CFG_S_TRAMONTO] == (1 << CFG_TRAMONTO) && c.tappeDaFile[CFG_S_ALBA] == (1 << CFG_ALBA));
  }

  // 3. custom values, CRLF, case, names, Grp_ syntax, forcing, wrap-around casette
  {
    const char *t =
      "[ciclo]\r\nDURATA1 = 30\r\ndurata2=120 ; two minutes\r\ndurata3 = 600\r\npot_min=10\r\npot_max = 680\r\n"
      "pot_invertito = no\r\npartenza = PAUSA\r\n"
      "[FASI]\ntramonto = 35.5\ncrepuscolo=48\nnotte=60\nalba=90\n"
      "[RELE]\nlogica = bassa\nnome1 = Mulino\nnome16 = Fontana\nforza2 = on\nforza3 = off\n"
      "[giorno]\nrele = Mulino, on, 10\n[NOTTE]\nrele = Grp_04_04, ON, 0\n[Alba]\nRELE = 16, spento, 100\n"
      "[COLORI]\nlum_alba = 50\ngamma = off\n"
      "[TRAMONTO]\ntramonto = 10, 200, 80, 20\nTRAMONTO = 60.5, 0, 0, 255, LINEARE\n"
      "[STELLE]\nnumero = 80\nattive = 30\ncolore = 100,100,100\n"
      "[CASETTE]\nnumero = 12\naccendi = NOTTE, 90\nspegni = GIORNO, 10\nfuoco = 0\n"
      "[SISTEMA]\nbuzzer = 0\ndebug_ms = 0\nfastboot = no\n";
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
    CHECK(c.numTappe[CFG_S_TRAMONTO] == 2 && c.tappeDaFile[CFG_S_TRAMONTO] == (1 << CFG_TRAMONTO));
    CHECK(c.tappe[CFG_S_TRAMONTO][0].col.r == 200 && c.tappe[CFG_S_TRAMONTO][0].curva == CURVA_MORBIDA);
    CHECK(std::fabs(c.tappe[CFG_S_TRAMONTO][1].pct - 60.5f) < 1e-4 && c.tappe[CFG_S_TRAMONTO][1].curva == CURVA_LINEARE);
    CHECK(c.numTappe[CFG_S_CIELO] == d.numTappe[CFG_S_CIELO] && c.tappeDaFile[CFG_S_CIELO] == 0);   // untouched strips keep defaults
    CHECK(c.numStelle == 80 && c.stelleAttive == 30 && c.coloreStelle.g == 100);
    CHECK(c.numCasette == 12 && c.accendiFase == CFG_NOTTE && c.accendiPct == 90 && c.spegniFase == CFG_GIORNO && c.fuoco == 0);
    CHECK(c.buzzer == 0 && c.debugMs == 0 && c.fastboot == 0 && d.fastboot == 1);
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
      "[NOTTE]\n"                 // 8
      "rele = 17, ON, 5\n"         // 9: relay out of range
      "rele = Grp_05_01, ON, 5\n"  // 10: bad group
      "evento = NOTTE, 1, ON, 5\n" // 11: old key (events now are "rele" lines in the phase sections)
      "rele = 1, FORSE, 5\n"       // 12: bad state
      "rele = 1, ON\n"             // 13: missing field
      "[RELE]\n"                   // 14
      "nome1 = NomeTroppoLungo\n"  // 15: > 12 chars
      "evento = NOTTE, 1, ON, 5\n" // 16: old key, removed from [RELE]
      "[COLORI]\n"                 // 17
      "giorno_caldo = 1,2,3\n"     // 18: old key, removed (now cielo tappe in the phase sections)
      "senza uguale\n";            // 19
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, t);
    std::printf("error case: %u errors, first line %u\n", c.errori, c.primaRigaErrata);
    CHECK(c.errori == 14); CHECK(c.primaRigaErrata == 1);
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
    std::string t = "[NOTTE]\n";
    for (int i = 0; i < 70; i++) t += "rele = 1, ON, 1\n";
    t += std::string(200, 'x') + "\n";
    PresepeConfig c; cfgDefault(c); cfgParseBuffer(c, t.c_str());
    CHECK(c.numEventi == CFG_MAX_EVENTI); CHECK(c.errori == 7);
  }

  // 7. colour tappe in the phase sections: interpolation, step, wrap-around, linear curve,
  //    per-phase replacement of the defaults, any order, errors
  {
    PresepeConfig c; cfgDefault(c);
    cfgParseBuffer(c,
      "[ALBA]\n"
      "cielo = 100, 0, 0, 0\n"                  // replaces the default ALBA tappe of the sky
      "alba = 50, 100, 0, 0\n"                  // only alba tappa: fixed colour all cycle
      "[TRAMONTO]\n"                             // sections in any order
      "cielo = 0, 200, 100, 0\n"
      "cielo = 0, 10, 10, 10\n"                 // same point: step, file order kept
      "cielo = 100, 1, 1, 1, veloce\n"          // ERROR: unknown curve
      "cielo = 120, 1, 1, 1\n"                  // ERROR: % > 100
      "cielo = 50, 1, 1\n"                      // ERROR: missing blue
      "cielo = 50, 1, 1, 1, lineare, x\n"       // ERROR: too many fields
      "colore = 1, 2, 3\n"                      // ERROR: unknown key
      "tappa = TRAMONTO, 0, 1, 1, 1\n"          // ERROR: old syntax
      "[GIORNO]\n"
      "cielo = 50, 200, 100, 0, lineare\n"      // written before the 0 % tappa: sorted anyway
      "cielo = 0, 0, 0, 0\n");
    CHECK(c.errori == 6); CHECK(c.numTappe[CFG_S_CIELO] == 5); CHECK(c.numTappe[CFG_S_ALBA] == 1);
    CHECK(c.tappe[CFG_S_CIELO][0].fase == CFG_GIORNO && c.tappe[CFG_S_CIELO][0].pct == 0.0f);
    CHECK(c.tappe[CFG_S_CIELO][1].pct == 50.0f && c.tappe[CFG_S_CIELO][4].fase == CFG_ALBA);
    CHECK(c.tappe[CFG_S_CIELO][2].col.r == 200 && c.tappe[CFG_S_CIELO][3].col.r == 10);
    // GIORNO = 0..40 % of the cycle: at 10 % (= 25 % of GIORNO, half way to the 50 % tappa) linear gives 100
    CfgRGB a = cfgColoreTappe(c, CFG_S_CIELO, 10.0f);
    CHECK(a.r == 100 && a.g == 50 && a.b == 0);
    CfgRGB b = cfgColoreTappe(c, CFG_S_CIELO, 30.0f);             // between 50 % GIORNO and TRAMONTO 0: constant
    CHECK(b.r == 200 && b.g == 100);
    CfgRGB s1 = cfgColoreTappe(c, CFG_S_CIELO, 40.0f);            // exactly at the step: the later tappa wins
    CHECK(s1.r == 10 && s1.g == 10 && s1.b == 10);
    CfgRGB w = cfgColoreTappe(c, CFG_S_CIELO, 70.0f);             // TRAMONTO 0 (40 %) -> ALBA 100 (100 %): x = 0.5
    CHECK(w.r == 5 && w.g == 5 && w.b == 5);
    CfgRGB z = cfgColoreTappe(c, CFG_S_CIELO, 0.0f);
    CHECK(z.r == 0 && z.g == 0 && z.b == 0);
    CfgRGB f1 = cfgColoreTappe(c, CFG_S_ALBA, 3.0f), f2 = cfgColoreTappe(c, CFG_S_ALBA, 95.0f);
    CHECK(f1.r == 100 && f2.r == 100 && f1.g == 0);
    CfgRGB t0 = cfgColoreTappe(c, CFG_S_TRAMONTO, 45.0f), td = cfgColoreTappe(d, CFG_S_TRAMONTO, 45.0f);
    CHECK(t0.r == td.r && t0.g == td.g && t0.b == td.b);           // tramonto strip not in the file: defaults
    // only one phase rewritten: the other phases keep their default tappe
    PresepeConfig e; cfgDefault(e);
    cfgParseBuffer(e, "[TRAMONTO]\ncielo = 0, 1, 2, 3\n");
    CHECK(e.errori == 0 && e.numTappe[CFG_S_CIELO] == d.numTappe[CFG_S_CIELO] - 4 + 1);
    CfgRGB noon = cfgColoreTappe(e, CFG_S_CIELO, 20.0f);           // GIORNO 50 %: default white plateau
    CHECK(noon.r == 255 && noon.g == 255 && noon.b == 255);
    // too many tappe: the 21st is an error
    std::string t = "[TRAMONTO]\n";
    for (int i = 0; i <= CFG_MAX_TAPPE; i++) t += "tramonto = " + std::to_string(i) + ", 1, 2, 3\n";
    PresepeConfig g; cfgDefault(g); cfgParseBuffer(g, t.c_str());
    CHECK(g.numTappe[CFG_S_TRAMONTO] == CFG_MAX_TAPPE && g.errori == 1);
  }
  // 8. default tappe: the historical key colours are where they used to be
  {
    CfgRGB g = cfgColoreTappe(d, CFG_S_CIELO, 0.0f), wh = cfgColoreTappe(d, CFG_S_CIELO, 40.0f * 0.50f);
    CHECK(g.r == 210 && g.g == 82 && g.b == 18); CHECK(wh.r == 255 && wh.g == 255 && wh.b == 255);   // noon plateau 45..55 %
    CfgRGB q = cfgColoreTappe(d, CFG_S_CIELO, cfgPosizione(d, CFG_GIORNO, 30.0f));
    CHECK(q.r == 246 && q.g == 220 && q.b == 208);
    CfgRGB pk = cfgColoreTappe(d, CFG_S_TRAMONTO, cfgPosizione(d, CFG_TRAMONTO, 10.0f));
    CHECK(pk.r == 155 && pk.g == 92 && pk.b == 16);
    CfgRGB off = cfgColoreTappe(d, CFG_S_ALBA, 20.0f), on = cfgColoreTappe(d, CFG_S_ALBA, cfgPosizione(d, CFG_ALBA, 1.0f));
    CHECK(off.r == 0 && off.g == 0 && off.b == 0); CHECK(on.r == 18 && on.g == 12 && on.b == 5);   // rapid 1 % switch-on
    CfgRGB a0 = cfgColoreTappe(d, CFG_S_ALBA, 85.0f);              // ALBA 0 %: still off
    CHECK(a0.r == 0 && a0.g == 0 && a0.b == 0);
    CfgRGB eod = cfgColoreTappe(d, CFG_S_CIELO, cfgPosizione(d, CFG_GIORNO, 100.0f));   // end of GIORNO: warm
    CHECK(eod.r == 210 && eod.g == 82 && eod.b == 18);
  }

  // 9. colour mode: potentiometer -> value (normal / fine) and pick-up
  {
    PresepeConfig c; cfgDefault(c);              // pot 0..1023, pot_invertito = 1
    CHECK(cfgValoreDaPot(c, 0, 0, 0) == 255 && cfgValoreDaPot(c, 1023, 0, 0) == 0);
    c.potInvertito = 0;
    CHECK(cfgValoreDaPot(c, 0, 0, 0) == 0 && cfgValoreDaPot(c, 1023, 0, 0) == 255);
    CHECK(cfgValoreDaPot(c, 512, 0, 0) == 128);
    CHECK(cfgValoreDaPot(c, -50, 0, 0) == 0 && cfgValoreDaPot(c, 2000, 0, 0) == 255);   // clamped
    // fine: whole travel = base -16 .. base +16, centre = base
    CHECK(cfgValoreDaPot(c, 0, 1, 100) == 84 && cfgValoreDaPot(c, 1023, 1, 100) == 116);
    CHECK(cfgValoreDaPot(c, 512, 1, 100) == 100);
    CHECK(cfgValoreDaPot(c, 0, 1, 5) == 0 && cfgValoreDaPot(c, 1023, 1, 250) == 255);    // clamped at 0 / 255
    CfgAggancio a; cfgAggancioReset(a);
    CHECK(cfgAggancioAggiorna(a, 100, 50) == 0 && cfgAggancioAggiorna(a, 100, 80) == 0);  // below, approaching
    CHECK(cfgAggancioAggiorna(a, 100, 99) == 1);                                           // reached (+/-1)
    CHECK(cfgAggancioAggiorna(a, 100, 10) == 1);                                           // stays engaged
    cfgAggancioReset(a);
    CHECK(cfgAggancioAggiorna(a, 100, 200) == 0 && cfgAggancioAggiorna(a, 100, 150) == 0); // above
    CHECK(cfgAggancioAggiorna(a, 100, 90) == 1);                                           // jumped past: engaged
    cfgAggancioReset(a);
    CHECK(cfgAggancioAggiorna(a, 100, 100) == 1);                                          // already on the value
  }

  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

/*
  PresepeController - Rev D (PCB PSN-Presepe Rev D)
  Arduino Mega 2560

  FILE UNICO: tutto il firmware sta in questo .ino (aprilo con l'IDE Arduino o
  copialo in sketch.ino su Wokwi). Il blocco CONFIG BEGIN/END contiene il lettore
  di PRESEPE.INI ed e' verificato su PC da firmware/tools/run_tests.sh.

  NOVITA' REV D
    - Parametri della scenografia letti all'avvio da microSD (/PRESEPE.INI, oppure
      /PRESEPE.TXT se manca il primo):
      durate del ciclo, fasi, schedulazione rele', nomi rele', colori,
      stelle, casette, buzzer/OLED/autotest. Senza SD -> valori di default.
    - microSD su SPI: D50 MISO, D51 MOSI, D52 SCK, D53 CS, D49 rilevamento scheda.
    - Potenziometro esterno su morsetto J_POT (5V / A0 / GND).
    - CASETTE (WS2811 su D8) ora si accendono nel ciclo secondo [CASETTE].

  USCITE (PCB Rev D)
    D2/D3/D4    = CIELO RGB R/G/B      -> MOSFET Q1-Q3 -> morsetto J_CIELO (12V comune, R-, G-, B-)
    D7/D11/D12  = TRAMONTO RGB R/G/B   -> MOSFET Q4-Q6 -> morsetto J_TRAMONTO
    D44/D45/D46 = ALBA RGB R/G/B       -> MOSFET Q7-Q9 -> morsetto J_ALBA
    D5          = DATA WS2811 STELLE   -> morsetto J_STELLE (12V / GND / DATA)
    D8          = DATA WS2811 CASETTE  -> morsetto J_CASETTE (12V / GND / DATA)
    D6          = buzzer passivo (220 ohm in serie, BZ1 sulla scheda)
    D25..D40    = rele' 1..16 tramite ULN2803A U1/U2 -> morsetti JR1..JR16 (COM/NO/NC)

  COMANDI (pulsanti NO verso GND, INPUT_PULLUP: nessuna resistenza esterna)
    D22 = START/STOP   (morsetto J_START)
    D23 = AVANTI/NEXT  (morsetto J_NEXT)
    D24 = TEST         (morsetto J_TEST)
    A0  = potenziometro lineare 10k esterno (morsetto J_POT: 5V / cursore / GND)

  DISPLAY E SD
    D20 SDA / D21 SCL = OLED SSD1306 128x64 I2C 0x3C (morsetto J_OLED: 5V / GND / SDA / SCL)
    D49 = rilevamento scheda microSD, D50-D53 = SPI microSD (MISO/MOSI/SCK/CS)

  CICLO
    GIORNO -> TRAMONTO -> CREPUSCOLO -> NOTTE -> ALBA -> GIORNO
    Il potenziometro sceglie fra tre durate del ciclo ([CICLO] durata1..3,
    default 1, 3 o 5 minuti).

  Serial Monitor: 115200 baud
*/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>
// (blocco CONFIG piu' sotto, prima della prima funzione: vedi CONFIG BEGIN)

// ============================================================
// CONFIGURAZIONE PIN
// ============================================================

const uint8_t PIN_CIELO_R = 2;
const uint8_t PIN_CIELO_G = 3;
const uint8_t PIN_CIELO_B = 4;
const uint8_t PIN_STELLE_DATA = 5;
const uint8_t PIN_CASETTE_DATA = 8; // seconda catena WS2811 CASETTE
const uint8_t PIN_BUZZER = 6; // piezo passivo opzionale: se assente il firmware funziona normalmente

// Strisce RGB laterali da 1 m, dedicate agli effetti direzionali.
// Sinistra = tramonto; destra = alba.
const uint8_t PIN_TRAMONTO_R = 7;
const uint8_t PIN_TRAMONTO_G = 11;
const uint8_t PIN_TRAMONTO_B = 12;
const uint8_t PIN_ALBA_R = 44;
const uint8_t PIN_ALBA_G = 45;
const uint8_t PIN_ALBA_B = 46;
#define NUM_STELLE (cfg.numStelle)        // da [STELLE] numero (max CFG_MAX_STELLE)
#define NUM_CASETTE (cfg.numCasette)      // da [CASETTE] numero (max CFG_MAX_CASETTE)
#define STELLE_ATTIVE (cfg.stelleAttive)  // da [STELLE] attive
#define STELLE_TREMOLANTI STELLE_ATTIVE     // tutte le stelle attive scintillano
Adafruit_NeoPixel stelle(50, PIN_STELLE_DATA, NEO_GRB + NEO_KHZ800);   // lunghezza aggiornata da cfg nel setup
Adafruit_NeoPixel casette(50, PIN_CASETTE_DATA, NEO_GRB + NEO_KHZ800); // lunghezza aggiornata da cfg nel setup

const uint8_t PIN_START = 22;
const uint8_t PIN_NEXT  = 23;
const uint8_t PIN_TEST  = 24;
const uint8_t PIN_POT   = A0;   // Rev D: potenziometro esterno su morsetto J_POT
const uint8_t PIN_SD_CS = 53;   // microSD chip select (SPI hardware: D50 MISO, D51 MOSI, D52 SCK)
const uint8_t PIN_SD_CD = 49;   // contatto "scheda inserita" dello slot (LOW = inserita)


// -----------------------------------------------------------------------------
// RELÈ: 16 uscite ON/OFF (G5Q-1 sulla PCB, pilotati da ULN2803A U1/U2).
// D25-D40 = rele' 1..16 (Grp_GG_RR: gruppo GG 01..04, rele' RR 01..04).
// La logica scenografica usa una tabella di schedulazione per fase/percentuale.
// -----------------------------------------------------------------------------
const uint8_t PIN_RELE[16] = {
  25, 26, 27, 28,
  29, 30, 31, 32,
  33, 34, 35, 36,
  37, 38, 39, 40
};

#define PWM_INVERTED (cfg.pwmInvertito)
#define RELE_ACTIVE_LOW (cfg.releAttivoBasso)   // [RELE] logica = alta|bassa
#define BUZZER_ENABLED (cfg.buzzer)             // [SISTEMA] buzzer = 0 silenzia tutto

// OLED ELEGOO EL-SM-008, 128x64, I2C 0x3C.
// Il display e' opzionale: se assente il presepe continua normalmente.
const uint8_t OLED_ADDR = 0x3C;
const uint8_t OLED_W = 128;
const uint8_t OLED_H = 64;
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
bool oledPresente = false;
unsigned long oledPopupFino = 0;
enum OledPopup : uint8_t { OLED_NESSUNO, OLED_PAUSA, OLED_RIPRESA, OLED_AVANTI, OLED_TEST, OLED_VELOCITA };
OledPopup oledPopup = OLED_NESSUNO;
int ultimoPotOled = -1;
int potRawOled = 0;
int potPopupUltimaLettura = -1;
unsigned long potPopupUltimaVariazioneMs = 0;
bool potPopupInAttesa = false;
const int POT_POPUP_DELTA = 60;
const unsigned long POT_POPUP_SETTLE_MS = 3000UL;
const unsigned long OLED_POPUP_MS = 1800UL;

// Tre velocita' discrete scelte dal potenziometro: durate da [CICLO] durata1..3
// (default 1, 3, 5 minuti). Corsa utile del pot da [CICLO] pot_min / pot_max.
#define MIN_CYCLE_MS (cfg.durataMs[0])
#define MID_CYCLE_MS (cfg.durataMs[1])
#define MAX_CYCLE_MS (cfg.durataMs[2])

// ==== CONFIG BEGIN ====
// Parametri da microSD: strutture, valori di default, lettore di PRESEPE.INI.
// Questo blocco (fino a CONFIG END) e' indipendente dall'hardware tranne le parti
// #ifdef ARDUINO: firmware/tools/run_tests.sh lo estrae e lo verifica su PC.
// =============================================================================
// PresepeConfig - parametri della scenografia caricati da microSD (PRESEPE.INI)
// =============================================================================
// Ogni parametro ha un valore di default identico al comportamento del firmware
// precedente. All'avvio il firmware legge /PRESEPE.INI dalla microSD: le chiavi
// presenti sostituiscono il default, quelle assenti o errate restano di default.
// Senza SD (o senza file) si usano solo i default.
//
// Il parser e' indipendente dall'hardware (cfgParseBuffer / cfgParseLine) e
// viene verificato anche su PC con test automatici (tools/test_config.cpp).
// =============================================================================
#include <stdint.h>

#define CFG_MAX_STELLE   100   // pixel WS2811 massimi per catena STELLE
#define CFG_MAX_CASETTE  100   // pixel WS2811 massimi per catena CASETTE
#define CFG_MAX_EVENTI    64   // righe "evento" della schedulazione rele'
#define CFG_NOME_LEN      13   // 12 caratteri + terminatore (nome rele' su OLED)
#define CFG_MAX_TAPPE     20   // tappe di colore massime per striscia RGB (CIELO / TRAMONTO / ALBA)
#define CFG_FILE_NAME     "PRESEPE.INI"   // file cercato per primo nella radice della microSD
#define CFG_FILE_NAME_ALT "PRESEPE.TXT"   // alternativa (Wokwi non accetta file .ini; utile anche su Windows)

enum CfgFase : uint8_t { CFG_GIORNO = 0, CFG_TRAMONTO, CFG_CREPUSCOLO, CFG_NOTTE, CFG_ALBA };

struct CfgRGB { uint8_t r, g, b; };

struct CfgEvento {          // { FASE, RELE', ACCESO, % DELLA FASE }
  uint8_t fase;             // CfgFase
  uint8_t rele;             // 0..15
  uint8_t acceso;           // 0/1
  uint8_t pct;              // 0..100
};

enum CfgForza : uint8_t { FORZA_AUTO = 0, FORZA_ON = 1, FORZA_OFF = 2 };

// Strisce RGB analogiche con colori a tappe ([CIELO], [TRAMONTO], [ALBA]).
enum CfgStriscia : uint8_t { CFG_S_CIELO = 0, CFG_S_TRAMONTO, CFG_S_ALBA, CFG_NUM_STRISCE };
// Come si arriva a una tappa partendo dalla precedente.
enum CfgCurva : uint8_t { CURVA_MORBIDA = 0,   // parte e arriva piano (smoothstep), come le dissolvenze storiche
                          CURVA_LINEARE = 1 }; // velocita' costante per tutto il tratto

struct CfgTappa {           // "tappa = FASE, %, R, G, B [, curva]"
  uint8_t fase;             // CfgFase
  uint8_t curva;            // CfgCurva del tratto che ARRIVA a questa tappa
  float   pct;              // 0..100 % della fase
  CfgRGB  col;              // colore da avere esattamente in quel punto
};

enum CfgSdStato : uint8_t {
  SD_NON_LETTA = 0,   // lettura non tentata
  SD_ASSENTE,         // nessuna scheda / init fallita
  SD_NO_FILE,         // scheda presente ma PRESEPE.INI mancante
  SD_OK,              // file letto senza errori
  SD_OK_ERRORI        // file letto, alcune righe ignorate (vedi primaRigaErrata)
};

struct PresepeConfig {
  // [CICLO]
  uint32_t durataMs[3];      // 3 scaglioni selezionati dal potenziometro
  int16_t  potMin, potMax;   // corsa utile letta su A0
  uint8_t  potInvertito;     // 1 = pot cablato al contrario (comportamento storico)
  uint8_t  partenzaInPausa;  // 1 = dopo il boot il ciclo parte in PAUSA
  // [FASI] inizio di ogni fase in % del ciclo (GIORNO parte da 0)
  float    pTramonto, pCrepuscolo, pNotte, pAlba;
  // [RELE]
  uint8_t  releAttivoBasso;
  uint8_t  forza[16];
  char     nome[16][CFG_NOME_LEN];
  CfgEvento eventi[CFG_MAX_EVENTI];
  uint8_t  numEventi;
  // [CIELO] [TRAMONTO] [ALBA] - colori a tappe, in ordine di tempo nel ciclo
  CfgTappa tappe[CFG_NUM_STRISCE][CFG_MAX_TAPPE];
  uint8_t  numTappe[CFG_NUM_STRISCE];
  // [COLORI]
  uint8_t  lumCielo, lumTramonto, lumAlba; // limite luminosita' 0..100 %
  uint8_t  gamma, pwmInvertito;
  // [STELLE]
  uint16_t numStelle;
  uint8_t  stelleAttive, lumStelleMin, lumStelleMax, livelloNotte;
  uint16_t scintillioMinMs, scintillioMaxMs;
  CfgRGB   coloreStelle;                  // tinta in % per canale (100,72,38 = bianco caldo)
  // [CASETTE]
  uint16_t numCasette;
  CfgRGB   coloreCasette;
  uint8_t  accendiFase, accendiPct, spegniFase, spegniPct;
  uint8_t  fuoco;                         // tremolio 0..100 (0 = luce fissa)
  uint16_t dissolvenzaMs;                 // tempo di accensione/spegnimento morbido
  // [SISTEMA]
  uint8_t  buzzer, melodiaAvvio, autotestAvvio, oled;
  uint16_t beepHz, beepMs;
  uint32_t debugMs;
  // stato lettura (non configurabile)
  uint8_t  sdStato;
  uint16_t righeLette, errori, primaRigaErrata;
  uint8_t  eventiDaFile;                  // 1 = la schedulazione arriva dal file
  uint8_t  tappeDaFile[CFG_NUM_STRISCE];  // 1 = le tappe di quella striscia arrivano dal file
};

extern PresepeConfig cfg;

void cfgDefault(PresepeConfig &c);
// Elabora una singola riga (modifica 'riga'). 'sezione' mantiene lo stato tra le righe.
void cfgParseLine(PresepeConfig &c, char *riga, char *sezione, uint16_t numeroRiga);
// Elabora un testo completo (usato dai test su PC).
void cfgParseBuffer(PresepeConfig &c, const char *testo);
// Controlli di coerenza finali (fasi crescenti, limiti, ecc.).
void cfgValida(PresepeConfig &c);
const char *cfgNomeFase(uint8_t f);
// Posizione nel ciclo (0..100 %) del punto "pct % della fase".
float cfgPosizione(const PresepeConfig &c, uint8_t fase, float pct);
// Colore della striscia 's' nel punto 'p' (0..100 %) del ciclo, calcolato dalle tappe.
CfgRGB cfgColoreTappe(const PresepeConfig &c, uint8_t s, float p);

#ifdef ARDUINO
// Legge /PRESEPE.INI dalla microSD (pin CS). Ritorna lo stato in cfg.sdStato.
uint8_t cfgCaricaDaSD(uint8_t pinCS, uint8_t pinCD);
// Nome del file effettivamente letto (CFG_FILE_NAME o CFG_FILE_NAME_ALT).
extern const char *cfgFileLetto;
#endif

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef ARDUINO
  #include <Arduino.h>
  #include <SPI.h>
  #include <SD.h>
#else
  #include <strings.h>
#endif

PresepeConfig cfg;

static const char *const NOMI_FASE[5] = { "GIORNO", "TRAMONTO", "CREPUSCOLO", "NOTTE", "ALBA" };
const char *cfgNomeFase(uint8_t f) { return f < 5 ? NOMI_FASE[f] : "?"; }

static CfgRGB rgb(uint8_t r, uint8_t g, uint8_t b) { CfgRGB c = { r, g, b }; return c; }

static void tappa(PresepeConfig &c, uint8_t s, uint8_t fase, float pct, uint8_t r, uint8_t g, uint8_t b) {
  CfgTappa &t = c.tappe[s][c.numTappe[s]++];
  t.fase = fase; t.pct = pct; t.col = rgb(r, g, b); t.curva = CURVA_MORBIDA;
}

// Curve storiche (fino al firmware Rev D senza tappe), tutte con curva morbida.
static void cfgTappeDefault(PresepeConfig &c) {
  for (uint8_t s = 0; s < CFG_NUM_STRISCE; s++) c.numTappe[s] = 0;
  // CIELO: caldo -> bianco pieno a 1/3 del GIORNO -> caldo a 2/3, si spegne nel TRAMONTO,
  // resta spento fino al 38 % dell'ALBA, poi riparte da un minimo caldo fino al GIORNO.
  tappa(c, CFG_S_CIELO, CFG_GIORNO,    0.0f, 210,  82,  18);
  tappa(c, CFG_S_CIELO, CFG_GIORNO,   33.33f, 255, 255, 255);
  tappa(c, CFG_S_CIELO, CFG_GIORNO,   66.67f, 210,  82,  18);
  tappa(c, CFG_S_CIELO, CFG_TRAMONTO,  0.0f, 210,  82,  18);
  tappa(c, CFG_S_CIELO, CFG_TRAMONTO, 30.0f,  16,  16,  16);
  tappa(c, CFG_S_CIELO, CFG_TRAMONTO, 38.0f,   0,   0,   0);
  tappa(c, CFG_S_CIELO, CFG_ALBA,     38.0f,   0,   0,   0);
  tappa(c, CFG_S_CIELO, CFG_ALBA,     38.0f,  10,   4,   1);
  // TRAMONTO (striscia sinistra): accesa solo nella fase TRAMONTO.
  tappa(c, CFG_S_TRAMONTO, CFG_TRAMONTO,   0.0f,   0,  0,  0);
  tappa(c, CFG_S_TRAMONTO, CFG_TRAMONTO,   0.0f,  18, 10,  4);
  tappa(c, CFG_S_TRAMONTO, CFG_TRAMONTO,  38.0f, 155, 92, 16);
  tappa(c, CFG_S_TRAMONTO, CFG_TRAMONTO,  82.0f,  16, 16, 16);
  tappa(c, CFG_S_TRAMONTO, CFG_TRAMONTO, 100.0f,   0,  0,  0);
  // ALBA (striscia destra): accesa solo nella fase ALBA.
  tappa(c, CFG_S_ALBA, CFG_ALBA,   0.0f,   0,  0,  0);
  tappa(c, CFG_S_ALBA, CFG_ALBA,   0.0f,  18, 12,  5);
  tappa(c, CFG_S_ALBA, CFG_ALBA,  38.0f, 155, 78, 22);
  tappa(c, CFG_S_ALBA, CFG_ALBA,  82.0f,  16, 16, 16);
  tappa(c, CFG_S_ALBA, CFG_ALBA, 100.0f,   0,  0,  0);
}

float cfgPosizione(const PresepeConfig &c, uint8_t fase, float pct) {
  const float inizio[5] = { 0.0f, c.pTramonto, c.pCrepuscolo, c.pNotte, c.pAlba };
  const float fine[5]   = { c.pTramonto, c.pCrepuscolo, c.pNotte, c.pAlba, 100.0f };
  if (fase > 4) fase = 4;
  return inizio[fase] + (fine[fase] - inizio[fase]) * pct / 100.0f;
}

// Stessa aritmetica di interpola8() dello sketch (troncamento).
static uint8_t interp8(uint8_t da, uint8_t a, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return (uint8_t)(da + ((float)a - da) * t);
}

CfgRGB cfgColoreTappe(const PresepeConfig &c, uint8_t s, float p) {
  const uint8_t n = c.numTappe[s];
  if (n == 0) return rgb(0, 0, 0);
  const CfgTappa *t = c.tappe[s];
  if (n == 1) return t[0].col;
  // tappa precedente = l'ultima con posizione <= p (a pari posizione vince l'ultima: "scatto")
  int8_t prec = -1;
  for (uint8_t i = 0; i < n; i++) if (cfgPosizione(c, t[i].fase, t[i].pct) <= p) prec = i;
  float pPrec, pSucc; uint8_t succ;
  if (prec < 0) {                       // prima della prima tappa: arriva dall'ultima del giro prima
    prec = n - 1; succ = 0;
    pPrec = cfgPosizione(c, t[prec].fase, t[prec].pct) - 100.0f;
    pSucc = cfgPosizione(c, t[0].fase, t[0].pct);
  } else {
    pPrec = cfgPosizione(c, t[prec].fase, t[prec].pct);
    if (prec + 1 < n) { succ = prec + 1; pSucc = cfgPosizione(c, t[succ].fase, t[succ].pct); }
    else { succ = 0; pSucc = cfgPosizione(c, t[0].fase, t[0].pct) + 100.0f; }   // giro del ciclo
  }
  float x = (pSucc > pPrec) ? (p - pPrec) / (pSucc - pPrec) : 1.0f;
  if (x < 0.0f) x = 0.0f;
  if (x > 1.0f) x = 1.0f;
  if (t[succ].curva == CURVA_MORBIDA) x = x * x * (3.0f - 2.0f * x);
  return rgb(interp8(t[prec].col.r, t[succ].col.r, x),
             interp8(t[prec].col.g, t[succ].col.g, x),
             interp8(t[prec].col.b, t[succ].col.b, x));
}

void cfgDefault(PresepeConfig &c) {
  memset(&c, 0, sizeof(c));
  // [CICLO] - 1 / 3 / 5 minuti, pot invertito come nel cablaggio storico.
  c.durataMs[0] = 60000UL; c.durataMs[1] = 180000UL; c.durataMs[2] = 300000UL;
  c.potMin = 0; c.potMax = 1023;  // Rev D: pot esterno alimentato a 5 V -> corsa 0..1023
  c.potInvertito = 1; c.partenzaInPausa = 0;
  // [FASI]
  c.pTramonto = 40.0f; c.pCrepuscolo = 50.0f; c.pNotte = 55.0f; c.pAlba = 85.0f;
  // [RELE] - logica alta, nessuna forzatura, schedulazione storica di esempio.
  c.releAttivoBasso = 0;
  c.eventi[0].fase = CFG_TRAMONTO; c.eventi[0].rele = 2; c.eventi[0].acceso = 1; c.eventi[0].pct = 30;
  c.eventi[1].fase = CFG_NOTTE;    c.eventi[1].rele = 2; c.eventi[1].acceso = 0; c.eventi[1].pct = 50;
  c.numEventi = 2;
  // [CIELO] [TRAMONTO] [ALBA] - tappe che riproducono le curve storiche del firmware.
  cfgTappeDefault(c);
  // [COLORI]
  c.lumCielo = 100; c.lumTramonto = 100; c.lumAlba = 100;
  c.gamma = 1; c.pwmInvertito = 0;
  // [STELLE]
  c.numStelle = 50; c.stelleAttive = 20; c.lumStelleMin = 22; c.lumStelleMax = 75; c.livelloNotte = 235;
  c.scintillioMinMs = 6400; c.scintillioMaxMs = 12200; c.coloreStelle = rgb(100, 72, 38);
  // [CASETTE] - luce calda dalla meta' del TRAMONTO a meta' ALBA, lieve tremolio.
  c.numCasette = 50; c.coloreCasette = rgb(90, 55, 20);
  c.accendiFase = CFG_TRAMONTO; c.accendiPct = 50; c.spegniFase = CFG_ALBA; c.spegniPct = 50;
  c.fuoco = 20; c.dissolvenzaMs = 3000;
  // [SISTEMA]
  c.buzzer = 1; c.melodiaAvvio = 1; c.autotestAvvio = 1; c.oled = 1;
  c.beepHz = 900; c.beepMs = 90; c.debugMs = 5000UL;
  c.sdStato = SD_NON_LETTA;
}

// ------------------------------------------------------------------ utilita'
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return s;
}
static bool ugualeCI(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static void errore(PresepeConfig &c, uint16_t riga) {
  c.errori++;
  if (!c.primaRigaErrata) c.primaRigaErrata = riga;
}

static bool leggiLong(const char *s, long &out, long minimo, long massimo) {
  char *fine;
  long v = strtol(s, &fine, 10);
  while (*fine == ' ' || *fine == '\t') fine++;
  if (fine == s || *fine) return false;
  if (v < minimo || v > massimo) return false;
  out = v; return true;
}
static bool leggiFloat(const char *s, float &out, float minimo, float massimo) {
  char *fine;
  double v = strtod(s, &fine);
  while (*fine == ' ' || *fine == '\t') fine++;
  if (fine == s || *fine) return false;
  if (v < minimo || v > massimo) return false;
  out = (float)v; return true;
}
static bool leggiBool(const char *s, uint8_t &out) {
  if (ugualeCI(s, "1") || ugualeCI(s, "si") || ugualeCI(s, "on") || ugualeCI(s, "true") || ugualeCI(s, "acceso")) { out = 1; return true; }
  if (ugualeCI(s, "0") || ugualeCI(s, "no") || ugualeCI(s, "off") || ugualeCI(s, "false") || ugualeCI(s, "spento")) { out = 0; return true; }
  return false;
}
// Divide "a, b, c" in campi (modifica la stringa). Ritorna il numero di campi.
static uint8_t campi(char *s, char **out, uint8_t max) {
  uint8_t n = 0;
  while (n < max) {
    char *virg = strchr(s, ',');
    if (virg) *virg = 0;
    out[n++] = trim(s);
    if (!virg) break;
    s = virg + 1;
  }
  return n;
}
static bool leggiRGB(char *s, CfgRGB &out) {
  char *f[4]; if (campi(s, f, 4) != 3) return false;
  long r, g, b;
  if (!leggiLong(f[0], r, 0, 255) || !leggiLong(f[1], g, 0, 255) || !leggiLong(f[2], b, 0, 255)) return false;
  out = rgb((uint8_t)r, (uint8_t)g, (uint8_t)b); return true;
}
static bool leggiFase(const char *s, uint8_t &out) {
  for (uint8_t i = 0; i < 5; i++) if (ugualeCI(s, NOMI_FASE[i])) { out = i; return true; }
  return false;
}
// Rele': numero 1..16, "Grp_GG_RR" (GG 01..04, RR 01..04) oppure un nome gia' definito.
static bool leggiRele(const PresepeConfig &c, const char *s, uint8_t &out) {
  long n;
  if (leggiLong(s, n, 1, 16)) { out = (uint8_t)(n - 1); return true; }
  if ((s[0] == 'G' || s[0] == 'g') && strlen(s) == 9 && (s[1] == 'r' || s[1] == 'R') && (s[2] == 'p' || s[2] == 'P')
      && s[3] == '_' && s[6] == '_' && isdigit(s[4]) && isdigit(s[5]) && isdigit(s[7]) && isdigit(s[8])) {
    int gg = (s[4] - '0') * 10 + (s[5] - '0'), rr = (s[7] - '0') * 10 + (s[8] - '0');
    if (gg >= 1 && gg <= 4 && rr >= 1 && rr <= 4) { out = (uint8_t)((gg - 1) * 4 + (rr - 1)); return true; }
    return false;
  }
  for (uint8_t i = 0; i < 16; i++) if (c.nome[i][0] && ugualeCI(s, c.nome[i])) { out = i; return true; }
  return false;
}
// "chiaveN" con N 1..16 -> indice 0..15, altrimenti -1
static int8_t suffissoRele(const char *chiave, const char *prefisso) {
  size_t lp = strlen(prefisso);
  if (strncasecmp(chiave, prefisso, lp) != 0) return -1;
  long n; if (!leggiLong(chiave + lp, n, 1, 16)) return -1;
  return (int8_t)(n - 1);
}

// ------------------------------------------------------------------ parser
static bool sezCiclo(PresepeConfig &c, const char *k, char *v) {
  long n;
  if (ugualeCI(k, "durata1") || ugualeCI(k, "durata2") || ugualeCI(k, "durata3")) {
    if (!leggiLong(v, n, 10, 86400)) return false;
    c.durataMs[k[6] - '1'] = (uint32_t)n * 1000UL; return true;
  }
  if (ugualeCI(k, "pot_min")) { if (!leggiLong(v, n, 0, 1023)) return false; c.potMin = (int16_t)n; return true; }
  if (ugualeCI(k, "pot_max")) { if (!leggiLong(v, n, 0, 1023)) return false; c.potMax = (int16_t)n; return true; }
  if (ugualeCI(k, "pot_invertito")) return leggiBool(v, c.potInvertito);
  if (ugualeCI(k, "partenza")) {
    if (ugualeCI(v, "marcia")) { c.partenzaInPausa = 0; return true; }
    if (ugualeCI(v, "pausa"))  { c.partenzaInPausa = 1; return true; }
    return false;
  }
  return false;
}
static bool sezFasi(PresepeConfig &c, const char *k, char *v) {
  float *dst = 0;
  if (ugualeCI(k, "tramonto")) dst = &c.pTramonto;
  else if (ugualeCI(k, "crepuscolo")) dst = &c.pCrepuscolo;
  else if (ugualeCI(k, "notte")) dst = &c.pNotte;
  else if (ugualeCI(k, "alba")) dst = &c.pAlba;
  if (!dst) return false;
  return leggiFloat(v, *dst, 1.0f, 99.0f);
}
static bool sezRele(PresepeConfig &c, const char *k, char *v) {
  int8_t i;
  if (ugualeCI(k, "logica")) {
    if (ugualeCI(v, "alta")) { c.releAttivoBasso = 0; return true; }
    if (ugualeCI(v, "bassa")) { c.releAttivoBasso = 1; return true; }
    return false;
  }
  if ((i = suffissoRele(k, "nome")) >= 0) {
    if (!*v || strlen(v) >= CFG_NOME_LEN || strchr(v, ',')) return false;
    strcpy(c.nome[i], v); return true;
  }
  if ((i = suffissoRele(k, "forza")) >= 0) {
    if (ugualeCI(v, "auto")) c.forza[i] = FORZA_AUTO;
    else if (ugualeCI(v, "on") || ugualeCI(v, "acceso")) c.forza[i] = FORZA_ON;
    else if (ugualeCI(v, "off") || ugualeCI(v, "spento")) c.forza[i] = FORZA_OFF;
    else return false;
    return true;
  }
  if (ugualeCI(k, "evento")) {
    char *f[5]; if (campi(v, f, 5) != 4) return false;
    CfgEvento e; uint8_t st; long pct;
    if (!leggiFase(f[0], e.fase) || !leggiRele(c, f[1], e.rele) || !leggiBool(f[2], st) || !leggiLong(f[3], pct, 0, 100)) return false;
    e.acceso = st; e.pct = (uint8_t)pct;
    if (!c.eventiDaFile) { c.numEventi = 0; c.eventiDaFile = 1; }   // il file sostituisce la tabella di default
    if (c.numEventi >= CFG_MAX_EVENTI) return false;
    c.eventi[c.numEventi++] = e; return true;
  }
  return false;
}
static bool sezColori(PresepeConfig &c, const char *k, char *v) {
  long n;
  if (ugualeCI(k, "lum_cielo")) { if (!leggiLong(v, n, 0, 100)) return false; c.lumCielo = (uint8_t)n; return true; }
  if (ugualeCI(k, "lum_tramonto")) { if (!leggiLong(v, n, 0, 100)) return false; c.lumTramonto = (uint8_t)n; return true; }
  if (ugualeCI(k, "lum_alba")) { if (!leggiLong(v, n, 0, 100)) return false; c.lumAlba = (uint8_t)n; return true; }
  if (ugualeCI(k, "gamma")) return leggiBool(v, c.gamma);
  if (ugualeCI(k, "pwm_invertito")) return leggiBool(v, c.pwmInvertito);
  return false;
}
// [CIELO] / [TRAMONTO] / [ALBA]:  tappa = FASE, %, R, G, B [, morbida|lineare]
static bool sezStriscia(PresepeConfig &c, uint8_t s, const char *k, char *v) {
  if (!ugualeCI(k, "tappa")) return false;
  char *f[7]; uint8_t nf = campi(v, f, 7);
  if (nf != 5 && nf != 6) return false;
  CfgTappa t; memset(&t, 0, sizeof(t)); float pct; long r, g, b;
  if (!leggiFase(f[0], t.fase) || !leggiFloat(f[1], pct, 0.0f, 100.0f) ||
      !leggiLong(f[2], r, 0, 255) || !leggiLong(f[3], g, 0, 255) || !leggiLong(f[4], b, 0, 255)) return false;
  t.pct = pct; t.col = rgb((uint8_t)r, (uint8_t)g, (uint8_t)b); t.curva = CURVA_MORBIDA;
  if (nf == 6) {
    if (ugualeCI(f[5], "morbida")) t.curva = CURVA_MORBIDA;
    else if (ugualeCI(f[5], "lineare")) t.curva = CURVA_LINEARE;
    else return false;
  }
  if (!c.tappeDaFile[s]) { c.numTappe[s] = 0; c.tappeDaFile[s] = 1; }   // il file sostituisce le tappe di default
  if (c.numTappe[s] >= CFG_MAX_TAPPE) return false;
  if (c.numTappe[s] > 0) {                                              // devono essere in ordine di tempo
    const CfgTappa &u = c.tappe[s][c.numTappe[s] - 1];
    if (t.fase < u.fase || (t.fase == u.fase && t.pct < u.pct)) return false;
  }
  c.tappe[s][c.numTappe[s]++] = t; return true;
}
static bool sezStelle(PresepeConfig &c, const char *k, char *v) {
  long n;
  if (ugualeCI(k, "numero")) { if (!leggiLong(v, n, 0, CFG_MAX_STELLE)) return false; c.numStelle = (uint16_t)n; return true; }
  if (ugualeCI(k, "attive")) { if (!leggiLong(v, n, 0, CFG_MAX_STELLE)) return false; c.stelleAttive = (uint8_t)n; return true; }
  if (ugualeCI(k, "lum_min")) { if (!leggiLong(v, n, 0, 255)) return false; c.lumStelleMin = (uint8_t)n; return true; }
  if (ugualeCI(k, "lum_max")) { if (!leggiLong(v, n, 0, 255)) return false; c.lumStelleMax = (uint8_t)n; return true; }
  if (ugualeCI(k, "livello_notte")) { if (!leggiLong(v, n, 0, 255)) return false; c.livelloNotte = (uint8_t)n; return true; }
  if (ugualeCI(k, "scintillio_min")) { if (!leggiLong(v, n, 500, 60000)) return false; c.scintillioMinMs = (uint16_t)n; return true; }
  if (ugualeCI(k, "scintillio_max")) { if (!leggiLong(v, n, 500, 60000)) return false; c.scintillioMaxMs = (uint16_t)n; return true; }
  if (ugualeCI(k, "colore")) {
    CfgRGB x; if (!leggiRGB(v, x) || x.r > 100 || x.g > 100 || x.b > 100) return false;
    c.coloreStelle = x; return true;
  }
  return false;
}
static bool posizione(char *v, uint8_t &fase, uint8_t &pct) {   // "FASE, %"
  char *f[3]; if (campi(v, f, 3) != 2) return false;
  long n; uint8_t fa;
  if (!leggiFase(f[0], fa) || !leggiLong(f[1], n, 0, 100)) return false;
  fase = fa; pct = (uint8_t)n; return true;
}
static bool sezCasette(PresepeConfig &c, const char *k, char *v) {
  long n;
  if (ugualeCI(k, "numero")) { if (!leggiLong(v, n, 0, CFG_MAX_CASETTE)) return false; c.numCasette = (uint16_t)n; return true; }
  if (ugualeCI(k, "colore")) return leggiRGB(v, c.coloreCasette);
  if (ugualeCI(k, "accendi")) return posizione(v, c.accendiFase, c.accendiPct);
  if (ugualeCI(k, "spegni")) return posizione(v, c.spegniFase, c.spegniPct);
  if (ugualeCI(k, "fuoco")) { if (!leggiLong(v, n, 0, 100)) return false; c.fuoco = (uint8_t)n; return true; }
  if (ugualeCI(k, "dissolvenza_ms")) { if (!leggiLong(v, n, 0, 60000)) return false; c.dissolvenzaMs = (uint16_t)n; return true; }
  return false;
}
static bool sezSistema(PresepeConfig &c, const char *k, char *v) {
  long n;
  if (ugualeCI(k, "buzzer")) return leggiBool(v, c.buzzer);
  if (ugualeCI(k, "melodia_avvio")) return leggiBool(v, c.melodiaAvvio);
  if (ugualeCI(k, "autotest_avvio")) return leggiBool(v, c.autotestAvvio);
  if (ugualeCI(k, "oled")) return leggiBool(v, c.oled);
  if (ugualeCI(k, "beep_hz")) { if (!leggiLong(v, n, 100, 8000)) return false; c.beepHz = (uint16_t)n; return true; }
  if (ugualeCI(k, "beep_ms")) { if (!leggiLong(v, n, 10, 2000)) return false; c.beepMs = (uint16_t)n; return true; }
  if (ugualeCI(k, "debug_ms")) { if (!leggiLong(v, n, 0, 3600000L)) return false; c.debugMs = (uint32_t)n; return true; }
  return false;
}

void cfgParseLine(PresepeConfig &c, char *riga, char *sezione, uint16_t numeroRiga) {
  c.righeLette = numeroRiga;
  for (char *p = riga; *p; p++) if (*p == ';' || *p == '#') { *p = 0; break; }   // commenti
  char *s = trim(riga);
  if (!*s) return;
  if (*s == '[') {
    char *e = strchr(s, ']');
    if (!e || e[1]) { errore(c, numeroRiga); sezione[0] = 0; return; }
    *e = 0; char *n = trim(s + 1);
    if (strlen(n) > 15) { errore(c, numeroRiga); sezione[0] = 0; return; }
    strcpy(sezione, n);
    if (!(ugualeCI(n, "CICLO") || ugualeCI(n, "FASI") || ugualeCI(n, "RELE") || ugualeCI(n, "COLORI") ||
          ugualeCI(n, "CIELO") || ugualeCI(n, "TRAMONTO") || ugualeCI(n, "ALBA") ||
          ugualeCI(n, "STELLE") || ugualeCI(n, "CASETTE") || ugualeCI(n, "SISTEMA"))) { errore(c, numeroRiga); sezione[0] = 0; }
    return;
  }
  char *uguale = strchr(s, '=');
  if (!uguale || !sezione[0]) { errore(c, numeroRiga); return; }
  *uguale = 0;
  char *k = trim(s), *v = trim(uguale + 1);
  bool ok = false;
  if (ugualeCI(sezione, "CICLO")) ok = sezCiclo(c, k, v);
  else if (ugualeCI(sezione, "FASI")) ok = sezFasi(c, k, v);
  else if (ugualeCI(sezione, "RELE")) ok = sezRele(c, k, v);
  else if (ugualeCI(sezione, "COLORI")) ok = sezColori(c, k, v);
  else if (ugualeCI(sezione, "CIELO")) ok = sezStriscia(c, CFG_S_CIELO, k, v);
  else if (ugualeCI(sezione, "TRAMONTO")) ok = sezStriscia(c, CFG_S_TRAMONTO, k, v);
  else if (ugualeCI(sezione, "ALBA")) ok = sezStriscia(c, CFG_S_ALBA, k, v);
  else if (ugualeCI(sezione, "STELLE")) ok = sezStelle(c, k, v);
  else if (ugualeCI(sezione, "CASETTE")) ok = sezCasette(c, k, v);
  else if (ugualeCI(sezione, "SISTEMA")) ok = sezSistema(c, k, v);
  if (!ok) errore(c, numeroRiga);
}

void cfgValida(PresepeConfig &c) {
  // Nessuna copia completa dei default (sarebbe ~1,6 kB sullo stack): i valori
  // di ripiego qui sotto sono gli stessi di cfgDefault().
  // fasi strettamente crescenti, altrimenti tutte di default
  if (!(c.pTramonto < c.pCrepuscolo && c.pCrepuscolo < c.pNotte && c.pNotte < c.pAlba)) {
    c.pTramonto = 40.0f; c.pCrepuscolo = 50.0f; c.pNotte = 55.0f; c.pAlba = 85.0f;
    errore(c, 0xFFFF);
  }
  if (c.potMax - c.potMin < 30) { c.potMin = 0; c.potMax = 1023; errore(c, 0xFFFF); }
  if (c.lumStelleMin > c.lumStelleMax) { c.lumStelleMin = 22; c.lumStelleMax = 75; errore(c, 0xFFFF); }
  if (c.scintillioMinMs > c.scintillioMaxMs) { c.scintillioMinMs = 6400; c.scintillioMaxMs = 12200; errore(c, 0xFFFF); }
  if (c.stelleAttive > c.numStelle) c.stelleAttive = (uint8_t)c.numStelle;
}

void cfgParseBuffer(PresepeConfig &c, const char *testo) {
  char riga[128], sezione[16] = "";
  uint16_t n = 0;
  while (*testo) {
    size_t l = 0;
    while (testo[l] && testo[l] != '\n') l++;
    n++;
    if (l >= sizeof(riga)) errore(c, n);
    else { memcpy(riga, testo, l); riga[l] = 0; cfgParseLine(c, riga, sezione, n); }
    testo += l; if (*testo == '\n') testo++;
  }
  cfgValida(c);
}

#ifdef ARDUINO
const char *cfgFileLetto = CFG_FILE_NAME;

uint8_t cfgCaricaDaSD(uint8_t pinCS, uint8_t pinCD) {
  (void)pinCD;   // il contatto di rilevamento e' solo informativo (vedi .ino)
  if (!SD.begin(pinCS)) { cfg.sdStato = SD_ASSENTE; return cfg.sdStato; }
  File f = SD.open(CFG_FILE_NAME, FILE_READ);
  if (!f) { f = SD.open(CFG_FILE_NAME_ALT, FILE_READ); if (f) cfgFileLetto = CFG_FILE_NAME_ALT; }
  if (!f) { cfg.sdStato = SD_NO_FILE; SD.end(); return cfg.sdStato; }
  char riga[128], sezione[16] = "";
  uint16_t n = 0; uint8_t l = 0; bool troppoLunga = false;
  while (true) {
    int ch = f.read();
    if (ch < 0 || ch == '\n') {
      if (ch < 0 && l == 0 && !troppoLunga) break;
      n++;
      riga[l] = 0;
      if (troppoLunga) errore(cfg, n); else cfgParseLine(cfg, riga, sezione, n);
      l = 0; troppoLunga = false;
      if (ch < 0) break;
    } else if (l < sizeof(riga) - 1) riga[l++] = (char)ch;
    else troppoLunga = true;
  }
  f.close();
  SD.end();
  cfgValida(cfg);
  cfg.sdStato = cfg.errori ? SD_OK_ERRORI : SD_OK;
  return cfg.sdStato;
}
#endif
// ==== CONFIG END ====

int potLimita(int raw) { return constrain(raw, (int)cfg.potMin, (int)cfg.potMax); }
unsigned long durataCicloStabile = 60000UL;
int potRawStabile = 0;

// Fasi in percentuale
#define P_TRAMONTO (cfg.pTramonto)   // [FASI] tramonto   (default 40)
#define P_CREPU    (cfg.pCrepuscolo) // [FASI] crepuscolo (default 50)
#define P_NOTTE    (cfg.pNotte)      // [FASI] notte      (default 55)
#define P_ALBA     (cfg.pAlba)       // [FASI] alba       (default 85)
// ALBA occupa l'ultimo 15% del ciclo e termina direttamente nel nuovo GIORNO.

const unsigned long DEBOUNCE_MS = 20;
#define DEBUG_INTERVAL_MS (cfg.debugMs)   // [SISTEMA] debug_ms (0 = nessun log periodico)

// ============================================================
// STATO
// ============================================================

enum Fase {
  GIORNO,
  TRAMONTO,
  CREPUSCOLO,
  NOTTE,
  ALBA
};

// Prototipi espliciti: il file compila anche senza la generazione automatica
// dei prototipi dell'IDE Arduino (arduino-cli / ctags non necessari).
enum OledPopup : uint8_t;
void buzzerBeep(bool comando = true);
void buzzerTick();
void buzzerPotTick(int raw);
int8_t pwmChannelIndex(uint8_t pin);
void pwmWrite(uint8_t pin, uint8_t value);
uint8_t scalaLum(uint8_t v, uint8_t pct);
void setCielo(uint8_t r, uint8_t g, uint8_t b);
void setTramonto(uint8_t r, uint8_t g, uint8_t b);
void setAlba(uint8_t r, uint8_t g, uint8_t b);
void generaCieloStellato();
void mostraStelle(float livello);
void setStelle(uint8_t value);
void tuttoSpento();
unsigned long durataDaRaw(int raw);
unsigned long durataCiclo();
void stampaDurataOled(unsigned long durata);
float percentualeCiclo(unsigned long durata);
float progresso(float p, float inizio, float fine);
uint8_t interpola8(uint8_t da, uint8_t a, float t);
Fase faseDaPercentuale(float p);
const char* nomeFase(Fase f);
float inizioFase(Fase f);
Fase faseSuccessiva(Fase f);
float fineFase(Fase f);
uint8_t indiceFase(Fase f);
void scriviRele(uint8_t indice, bool acceso);
void aggiornaReleSchedulati(float p);
float posizioneCiclo(uint8_t fase, uint8_t pct);
bool casetteAccese(float p);
void aggiornaCasette(float p);
float percentualeFase(float p, Fase f);
void oledCentro(const __FlashStringHelper *s, int y, uint8_t size = 1);
void mostraOledTest();
void mostraOledRgbPausa(float p);
void aggiornaOled(unsigned long durata, float p);
bool inizializzaOled();
void mostraStatoSD();
void saltaAPercentuale(float p);
void faseAvanti();
void aggiornaScena(float p);
bool pulsantePremuto(uint8_t pin, bool &lastRead, bool &stable, unsigned long &lastChange);
void toggleStartStop();
void spegniRele();
void accendiRele(uint8_t indice);
void applicaTestCorrente();
void testUscite();
void stampaStato(unsigned long durata, float p);
void stampaConfig();
void aggiornaMelodiaBoot(unsigned long elapsedTotale);
void mostraOledBoot(const __FlashStringHelper *fase, uint8_t step, unsigned long elapsedStep);
void attesaBoot(const __FlashStringHelper *fase, uint8_t step);
void eseguiSequenzaBoot();


// ============================================================
// SCHEDULAZIONE RELÈ
// ============================================================
// La tabella eventi e' in cfg.eventi (da [RELE] evento = FASE, RELE', ON|OFF, %).
// Ogni evento e' persistente: il rele' mantiene lo stato fino al successivo
// evento che lo riguarda. Default (senza SD): Grp_01_03 ON al 30% del TRAMONTO,
// OFF al 50% della NOTTE. [RELE] forzaN = on|off|auto scavalca la tabella.
#define NUM_EVENTI_RELE (cfg.numEventi)

bool running = true;
bool testInCorso = false;
uint8_t testIndice = 0;
bool testEraInMarcia = false;
unsigned long testPausaMs = 0;

unsigned long cycleStartMs = 0;
unsigned long pauseStartedMs = 0;
unsigned long lastDebugMs = 0;

// Ultimi valori RGB realmente richiesti alle tre strisce.
// Servono anche per mostrarli sul display quando il ciclo viene messo in pausa.
uint8_t rgbCieloR = 0, rgbCieloG = 0, rgbCieloB = 0;
uint8_t rgbTramontoR = 0, rgbTramontoG = 0, rgbTramontoB = 0;
uint8_t rgbAlbaR = 0, rgbAlbaG = 0, rgbAlbaB = 0;

// debounce pulsanti
bool lastStartRead = HIGH, stableStart = HIGH;
bool lastNextRead  = HIGH, stableNext  = HIGH;
bool lastTestRead  = HIGH, stableTest  = HIGH;
unsigned long dbStartMs = 0, dbNextMs = 0, dbTestMs = 0;

// Feedback acustico semplice e non bloccante.
// Tutti i comandi (START/STOP, AVANTI, TEST e potenziometro) usano lo stesso bip.
#define BEEP_HZ (cfg.beepHz)   // [SISTEMA] beep_hz
#define BEEP_MS (cfg.beepMs)   // [SISTEMA] beep_ms
unsigned long buzzerFino = 0;
bool buzzerAttivo = false;
bool buzzerPrioritaComando = false;
int potBeepRiferimento = -1;
int potBeepUltimaLettura = -1;
unsigned long potUltimaVariazioneMs = 0;
bool potBeepInAttesa = false;
const int POT_BEEP_DELTA = 60;                 // circa 8% della corsa: ignora piccoli spostamenti/rumore
const int POT_BEEP_STABILITA_DELTA = 4;        // entro 4 punti ADC consideriamo il pot fermo
const unsigned long POT_BEEP_SETTLE_MS = 350UL; // bip solo 350 ms dopo l'ultima variazione

void buzzerBeep(bool comando) {
  if (!BUZZER_ENABLED) return;

  // I comandi fisici hanno priorita': il feedback del potenziometro
  // non puo' troncare o sostituire un bip START/STOP, AVANTI o TEST.
  if (!comando && buzzerAttivo && buzzerPrioritaComando) return;

  // Ripartenza esplicita: ogni comando riconosciuto ottiene un bip completo.
  noTone(PIN_BUZZER);
  tone(PIN_BUZZER, BEEP_HZ);
  buzzerFino = millis() + BEEP_MS;
  buzzerAttivo = true;
  buzzerPrioritaComando = comando;
}

void buzzerTick() {
  if (buzzerAttivo && (long)(millis() - buzzerFino) >= 0) {
    noTone(PIN_BUZZER);
    buzzerAttivo = false;
    buzzerPrioritaComando = false;
  }
}

void buzzerPotTick(int raw) {
  unsigned long now = millis();

  if (potBeepRiferimento < 0) {
    potBeepRiferimento = raw;
    potBeepUltimaLettura = raw;
    return;
  }

  // Segui il movimento reale del potenziometro, ignorando il normale rumore ADC.
  if (abs(raw - potBeepUltimaLettura) >= POT_BEEP_STABILITA_DELTA) {
    potBeepUltimaLettura = raw;
    potUltimaVariazioneMs = now;

    // Il bip viene armato solo dopo uno spostamento consistente.
    if (abs(raw - potBeepRiferimento) >= POT_BEEP_DELTA)
      potBeepInAttesa = true;
  }

  // Un solo bip quando la manopola e' rimasta ferma per qualche centinaio di ms.
  if (potBeepInAttesa && now - potUltimaVariazioneMs >= POT_BEEP_SETTLE_MS) {
    potBeepInAttesa = false;
    potBeepRiferimento = raw;
    potBeepUltimaLettura = raw;
    buzzerBeep(false);
  }
}

// ============================================================
// PWM
// ============================================================

// Correzione percettiva per le strisce RGB analogiche.
// La scenografia lavora 0..255 e analogWrite() resta PWM hardware a 8 bit.
// Applichiamo una curva gamma ~2.0 e arrotondiamo al gradino PWM piu' vicino.
// Nessun dithering temporale: evitiamo l'alternanza tra gradini alle basse luci.
#define PWM_GAMMA (cfg.gamma)   // [COLORI] gamma

int8_t pwmChannelIndex(uint8_t pin) {
  const uint8_t pins[9] = {
    PIN_CIELO_R, PIN_CIELO_G, PIN_CIELO_B,
    PIN_TRAMONTO_R, PIN_TRAMONTO_G, PIN_TRAMONTO_B,
    PIN_ALBA_R, PIN_ALBA_G, PIN_ALBA_B
  };
  for (uint8_t i = 0; i < 9; i++)
    if (pins[i] == pin) return i;
  return -1;
}

void pwmWrite(uint8_t pin, uint8_t value) {
  uint8_t out = value;

  if (PWM_GAMMA) {
    // Gamma 2.0 calcolata in fixed point; il risultato viene poi
    // arrotondato al gradino PWM hardware 8-bit piu' vicino.
    uint32_t squared = (uint32_t)value * (uint32_t)value;
    uint16_t pwm16 = (uint16_t)((squared * 4080UL + 32512UL) / 65025UL);
    uint8_t base = pwm16 >> 4;
    uint8_t frac = pwm16 & 0x0F;

    // Nessun dithering temporale: manteniamo la gamma e arrotondiamo
    // semplicemente al gradino PWM 8-bit piu' vicino.
    // Questo elimina l'alternanza tra gradini che sull'hardware reale
    // viene percepita come lampeggio alle bassissime luminosita'.
    if (frac >= 8 && base < 255) base++;
    out = base;
  }

  analogWrite(pin, PWM_INVERTED ? (255 - out) : out);
}

// Limite di luminosita' per striscia ([COLORI] lum_cielo/lum_tramonto/lum_alba, 0..100 %).
// Il valore richiesto dalla scena viene memorizzato (OLED in pausa) e scalato solo in uscita.
uint8_t scalaLum(uint8_t v, uint8_t pct) { return pct >= 100 ? v : (uint8_t)(((uint16_t)v * pct + 50) / 100); }

void setCielo(uint8_t r, uint8_t g, uint8_t b) {
  rgbCieloR = r; rgbCieloG = g; rgbCieloB = b;
  pwmWrite(PIN_CIELO_R, scalaLum(r, cfg.lumCielo));
  pwmWrite(PIN_CIELO_G, scalaLum(g, cfg.lumCielo));
  pwmWrite(PIN_CIELO_B, scalaLum(b, cfg.lumCielo));
}

void setTramonto(uint8_t r, uint8_t g, uint8_t b) {
  rgbTramontoR = r; rgbTramontoG = g; rgbTramontoB = b;
  pwmWrite(PIN_TRAMONTO_R, scalaLum(r, cfg.lumTramonto));
  pwmWrite(PIN_TRAMONTO_G, scalaLum(g, cfg.lumTramonto));
  pwmWrite(PIN_TRAMONTO_B, scalaLum(b, cfg.lumTramonto));
}

void setAlba(uint8_t r, uint8_t g, uint8_t b) {
  rgbAlbaR = r; rgbAlbaG = g; rgbAlbaB = b;
  pwmWrite(PIN_ALBA_R, scalaLum(r, cfg.lumAlba));
  pwmWrite(PIN_ALBA_G, scalaLum(g, cfg.lumAlba));
  pwmWrite(PIN_ALBA_B, scalaLum(b, cfg.lumAlba));
}

// ============================================================
// STELLE WS2811
// ============================================================
// Effetto naturale: ogni notte viene generato un cielo diverso.
// Ogni notte vengono scelte casualmente 20 stelle su 50, con luminosita'
// massime differenti. Compaiono nel crepuscolo, restano attive di notte e
// scompaiono durante l'alba. Tutte le 20 hanno variazioni asincrone individuali.

uint8_t stellaLum[CFG_MAX_STELLE];
uint8_t stellaOrdine[CFG_MAX_STELLE];
bool stellaTwinkle[CFG_MAX_STELLE];
bool cieloGenerato = false;
Fase ultimaFaseStelle = GIORNO;

void generaCieloStellato() {
  // Ogni notte scegliamo casualmente solo 20 delle 50 stelle fisiche.
  // Le altre 30 restano completamente spente.
  for (uint16_t i = 0; i < NUM_STELLE; i++) {
    stellaLum[i] = random(cfg.lumStelleMin, cfg.lumStelleMax + 1); // [STELLE] lum_min..lum_max (tenue)
    stellaOrdine[i] = i;
    stellaTwinkle[i] = false;
  }

  // Fisher-Yates su tutti i 50 pixel: i primi 20 saranno quelli attivi.
  for (int i = NUM_STELLE - 1; i > 0; i--) {
    int j = random(i + 1);
    uint8_t tmp = stellaOrdine[i];
    stellaOrdine[i] = stellaOrdine[j];
    stellaOrdine[j] = tmp;
  }

  // Tutte le 20 stelle attive ricevono il comportamento variabile.
  // Il piccolo shuffle assegna il flag alle stelle gia' selezionate casualmente;
  // STELLE_TREMOLANTI coincide attualmente con STELLE_ATTIVE, quindi sono tutte.
  uint8_t candidati[CFG_MAX_STELLE];
  for (uint8_t i = 0; i < STELLE_ATTIVE; i++) candidati[i] = i;
  for (int i = STELLE_ATTIVE - 1; i > 0; i--) {
    int j = random(i + 1);
    uint8_t tmp = candidati[i];
    candidati[i] = candidati[j];
    candidati[j] = tmp;
  }
  for (uint8_t n = 0; n < STELLE_TREMOLANTI; n++)
    stellaTwinkle[stellaOrdine[candidati[n]]] = true;

  cieloGenerato = true;
}

void mostraStelle(float livello) {
  livello = constrain(livello, 0.0f, 1.0f);
  stelle.clear();

  // Durante il crepuscolo entrano progressivamente le 20 stelle scelte;
  // durante l'alba scompaiono progressivamente. Mai piu' di 20 accese.
  uint8_t visibili = (uint8_t)(livello * STELLE_ATTIVE + 0.5f);
  if (visibili > STELLE_ATTIVE) visibili = STELLE_ATTIVE;

  for (uint8_t pos = 0; pos < visibili; pos++) {
    uint8_t i = stellaOrdine[pos];
    float locale = livello * STELLE_ATTIVE - pos;
    locale = constrain(locale, 0.0f, 1.0f);

    uint16_t v = (uint16_t)(stellaLum[i] * locale);

    // Ogni stella attiva segue autonomamente un ciclo di luminosita':
    // livello pieno -> dissolvenza a zero -> pausa spenta -> riaccensione.
    // Durate e offset differenti evitano che le 20 stelle si muovano insieme.
    if (stellaTwinkle[i] && v > 5) {
      // Durata pseudo-casuale e stabile per ogni pixel: circa 6,4-12,2 s.
      // Rev037: raddoppiata per rendere accensione/spegnimento piu' lento dal vero.
      // Anche l'offset e' diverso per ogni stella, cosi' partono vicine ma non insieme
      // e col tempo si sfasano sempre di piu'.
      // [STELLE] scintillio_min..scintillio_max (default 6400..12200 ms, 30 gradini).
      const unsigned long passo = ((unsigned long)cfg.scintillioMaxMs - cfg.scintillioMinMs) / 29UL;
      const unsigned long periodo = cfg.scintillioMinMs + ((unsigned long)(i * 37U) % 30UL) * passo;
      const unsigned long offset = ((unsigned long)(i * 173U) % 900UL);
      const unsigned long faseMs = (millis() + offset) % periodo;
      const unsigned long pTw = (faseMs * 100UL) / periodo;
      uint8_t fattore = 100;

      if (pTw < 45) {
        fattore = 100;                              // piena luminosita'
      } else if (pTw < 55) {
        fattore = (uint8_t)((55UL - pTw) * 10UL);  // 100 -> 0
      } else if (pTw < 80) {
        fattore = 0;                                // SPENTA per il 25% del ciclo
      } else if (pTw < 90) {
        fattore = (uint8_t)((pTw - 80UL) * 10UL);  // 0 -> 100
      } else {
        fattore = 100;
      }

      // A fattore zero scriviamo esplicitamente nero sul pixel.
      if (fattore == 0) {
        v = 0;
      } else {
        v = (v * fattore) / 100U;
      }
    }

    // Bianco caldo tenue.
    // [STELLE] colore = tinta in % per canale (default 100,72,38 = bianco caldo).
    stelle.setPixelColor(i, stelle.Color(
      (uint8_t)((v * cfg.coloreStelle.r) / 100U),
      (uint8_t)((v * cfg.coloreStelle.g) / 100U),
      (uint8_t)((v * cfg.coloreStelle.b) / 100U)
    ));
  }
  // NeoPixel disabilita gli interrupt durante la trasmissione.
  // Limitiamo il refresh a 50 Hz per non alterare millis()/Timer0.
  static unsigned long ultimoShowStelleUs = 0;
  unsigned long adessoUs = micros();
  if ((unsigned long)(adessoUs - ultimoShowStelleUs) >= 20000UL) {
    stelle.show();
    ultimoShowStelleUs = adessoUs;
  }
}

void setStelle(uint8_t value) {
  if (!cieloGenerato) generaCieloStellato();
  mostraStelle(value / 255.0f);
}

void tuttoSpento() {
  setCielo(0, 0, 0);
  setTramonto(0, 0, 0);
  setAlba(0, 0, 0);
  setStelle(0);
  casette.clear();
  casette.show();
}

// ============================================================
// TEMPO / POTENZIOMETRO
// ============================================================

unsigned long durataDaRaw(int raw) {
  raw = potLimita(raw);

  // Tre intervalli uguali della corsa utile [pot_min..pot_max].
  // Con pot_invertito = 1 (cablaggio storico) raw alto = ciclo piu' breve.
  int corsa = cfg.potMax - cfg.potMin;
  int x = cfg.potInvertito ? (cfg.potMax - raw) : (raw - cfg.potMin);
  if (x < (corsa + 1) / 3) return MIN_CYCLE_MS;
  if (x < 2 * (corsa + 1) / 3) return MID_CYCLE_MS;
  return MAX_CYCLE_MS;
}

unsigned long durataCiclo() {
  // La durata usata dal ciclo NON segue il rumore istantaneo dell'ADC.
  return durataCicloStabile;
}

void stampaDurataOled(unsigned long durata) {
  unsigned long secondi = (durata + 500UL) / 1000UL;
  unsigned int mm = secondi / 60UL;
  unsigned int ss = secondi % 60UL;
  if (mm < 10) display.print('0');
  display.print(mm);
  display.print(':');
  if (ss < 10) display.print('0');
  display.print(ss);
}

float percentualeCiclo(unsigned long durata) {
  unsigned long riferimento = running ? millis() : pauseStartedMs;
  unsigned long elapsed = riferimento - cycleStartMs;
  elapsed %= durata;
  return (elapsed * 100.0f) / durata;
}

float progresso(float p, float inizio, float fine) {
  if (fine <= inizio) return 0.0f;
  return constrain((p - inizio) / (fine - inizio), 0.0f, 1.0f);
}

uint8_t interpola8(uint8_t da, uint8_t a, float t) {
  t = constrain(t, 0.0f, 1.0f);
  return (uint8_t)(da + ((float)a - da) * t);
}

// ============================================================
// FASI
// ============================================================

Fase faseDaPercentuale(float p) {
  if (p < P_TRAMONTO) return GIORNO;
  if (p < P_CREPU)    return TRAMONTO;
  if (p < P_NOTTE)    return CREPUSCOLO;
  if (p < P_ALBA)     return NOTTE;
  return ALBA;
}

const char* nomeFase(Fase f) {
  switch (f) {
    case GIORNO:        return "GIORNO";
    case TRAMONTO:      return "TRAMONTO";
    case CREPUSCOLO:    return "CREPUSCOLO";
    case NOTTE:         return "NOTTE";
    case ALBA:          return "ALBA";
  }
  return "?";
}

float inizioFase(Fase f) {
  switch (f) {
    case GIORNO:        return 0.0f;
    case TRAMONTO:      return P_TRAMONTO;
    case CREPUSCOLO:    return P_CREPU;
    case NOTTE:         return P_NOTTE;
    case ALBA:          return P_ALBA;
  }
  return 0.0f;
}

Fase faseSuccessiva(Fase f) {
  switch (f) {
    case GIORNO:        return TRAMONTO;
    case TRAMONTO:      return CREPUSCOLO;
    case CREPUSCOLO:    return NOTTE;
    case NOTTE:         return ALBA;
    case ALBA:          return GIORNO;
  }
  return GIORNO;
}

float fineFase(Fase f) {
  switch (f) {
    case GIORNO:     return P_TRAMONTO;
    case TRAMONTO:   return P_CREPU;
    case CREPUSCOLO: return P_NOTTE;
    case NOTTE:      return P_ALBA;
    case ALBA:       return 100.0f;
  }
  return 100.0f;
}

uint8_t indiceFase(Fase f) {
  return (uint8_t)f;
}

void scriviRele(uint8_t indice, bool acceso) {
  if (indice >= 16) return;
  digitalWrite(PIN_RELE[indice],
               acceso ? (RELE_ACTIVE_LOW ? LOW : HIGH)
                      : (RELE_ACTIVE_LOW ? HIGH : LOW));
}

// Ricostruisce lo stato dei 16 relè direttamente dalla posizione corrente
// del ciclo. Questo rende la schedulazione deterministica anche dopo AVANTI,
// pausa/ripresa o variazioni della durata del ciclo.
void aggiornaReleSchedulati(float p) {
  Fase faseAttuale = faseDaPercentuale(p);
  float inizio = inizioFase(faseAttuale);
  float fine = fineFase(faseAttuale);
  float pctFase = (fine > inizio)
                    ? constrain((p - inizio) * 100.0f / (fine - inizio), 0.0f, 100.0f)
                    : 0.0f;

  for (uint8_t r = 0; r < 16; r++) {
    bool stato = false;
    bool trovato = false;
    uint16_t posizioneMigliore = 0;

    if (cfg.forza[r] == FORZA_ON)  { scriviRele(r, true);  continue; }
    if (cfg.forza[r] == FORZA_OFF) { scriviRele(r, false); continue; }

    // Sceglie l'evento cronologicamente piu' recente gia' raggiunto,
    // indipendentemente dall'ordine delle righe nella tabella.
    for (uint8_t i = 0; i < NUM_EVENTI_RELE; i++) {
      const CfgEvento &ev = cfg.eventi[i];
      if (ev.rele != r) continue;

      bool fasePassata = ev.fase < indiceFase(faseAttuale);
      bool faseCorrenteRaggiunta =
        ev.fase == indiceFase(faseAttuale) && pctFase >= ev.pct;
      if (!fasePassata && !faseCorrenteRaggiunta) continue;

      uint16_t posizione = (uint16_t)ev.fase * 101U + ev.pct;
      if (!trovato || posizione >= posizioneMigliore) {
        trovato = true;
        posizioneMigliore = posizione;
        stato = ev.acceso;
      }
    }

    scriviRele(r, stato);
  }
}

// ============================================================
// CASETTE WS2811 (seconda catena su D8) - parametri [CASETTE]
// ============================================================
// Accese tra "accendi = FASE, %" e "spegni = FASE, %" (anche a cavallo di fine
// ciclo), con dissolvenza morbida e tremolio tipo fuoco opzionale per pixel.
uint8_t casettaFuoco[CFG_MAX_CASETTE];
uint8_t casettaTarget[CFG_MAX_CASETTE];
float casetteLivello = 0.0f;
unsigned long casetteUltimoMs = 0;

float posizioneCiclo(uint8_t fase, uint8_t pct) {
  float a = inizioFase((Fase)fase), b = fineFase((Fase)fase);
  return a + (b - a) * pct / 100.0f;
}

bool casetteAccese(float p) {
  float a = posizioneCiclo(cfg.accendiFase, cfg.accendiPct);
  float b = posizioneCiclo(cfg.spegniFase, cfg.spegniPct);
  if (a == b) return false;
  return (a < b) ? (p >= a && p < b) : (p >= a || p < b);
}

void aggiornaCasette(float p) {
  if (NUM_CASETTE == 0) return;
  unsigned long now = millis();
  unsigned long dt = now - casetteUltimoMs;
  if (dt < 20) return;              // max 50 Hz: NeoPixel sospende gli interrupt
  casetteUltimoMs = now;
  if (dt > 200) dt = 200;

  float obiettivo = casetteAccese(p) ? 1.0f : 0.0f;
  float passo = cfg.dissolvenzaMs ? (float)dt / (float)cfg.dissolvenzaMs : 1.0f;
  if (casetteLivello < obiettivo) casetteLivello = min(obiettivo, casetteLivello + passo);
  else if (casetteLivello > obiettivo) casetteLivello = max(obiettivo, casetteLivello - passo);

  for (uint16_t i = 0; i < NUM_CASETTE; i++) {
    uint8_t f = 255;
    if (cfg.fuoco) {
      if (random(6) == 0)
        casettaTarget[i] = 255 - (uint8_t)random(0, (long)cfg.fuoco * 255L / 100L + 1L);
      int16_t d = (int16_t)casettaTarget[i] - (int16_t)casettaFuoco[i];
      casettaFuoco[i] = (uint8_t)(casettaFuoco[i] + d / 3);
      f = casettaFuoco[i];
    }
    float k = casetteLivello * f / 255.0f;
    casette.setPixelColor(i, casette.Color((uint8_t)(cfg.coloreCasette.r * k),
                                           (uint8_t)(cfg.coloreCasette.g * k),
                                           (uint8_t)(cfg.coloreCasette.b * k)));
  }
  casette.show();
}

// ============================================================
// OLED
// ============================================================

void oledMostraPopup(OledPopup tipo) {
  if (!oledPresente) return;
  oledPopup = tipo;
  oledPopupFino = millis() + OLED_POPUP_MS;
}

float percentualeFase(float p, Fase f) {
  float a = inizioFase(f), b = 100.0f;
  switch (f) {
    case GIORNO: b = P_TRAMONTO; break;
    case TRAMONTO: b = P_CREPU; break;
    case CREPUSCOLO: b = P_NOTTE; break;
    case NOTTE: b = P_ALBA; break;
    case ALBA: b = 100.0f; break;
  }
  return constrain((p-a)*100.0f/(b-a),0.0f,100.0f);
}

void oledCentro(const __FlashStringHelper *s, int y, uint8_t size) {
  display.setTextSize(size);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(s,0,y,&x1,&y1,&w,&h);
  display.setCursor((OLED_W-w)/2,y);
  display.print(s);
}

void mostraOledTest() {
  if (!oledPresente) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0); display.print(F("MODALITA' TEST"));
  display.setCursor(0,18); display.print(F("Test ")); display.print(testIndice + 1); display.print(F("/34"));
  display.setCursor(0,32);
  if (testIndice < 18) {
    switch (testIndice) {
      case 0: display.print(F("CIELO ROSSO")); break;
      case 1: display.print(F("CIELO VERDE")); break;
      case 2: display.print(F("CIELO BLU")); break;
      case 3: display.print(F("TRAMONTO ROSSO")); break;
      case 4: display.print(F("TRAMONTO VERDE")); break;
      case 5: display.print(F("TRAMONTO BLU")); break;
      case 6: display.print(F("ALBA ROSSO")); break;
      case 7: display.print(F("ALBA VERDE")); break;
      case 8: display.print(F("ALBA BLU")); break;
      case 9: display.print(F("STELLE ROSSE")); break;
      case 10: display.print(F("STELLE VERDI")); break;
      case 11: display.print(F("STELLE BLU")); break;
      case 12: display.print(F("STELLE WS2811")); break;
      case 13: display.print(F("CASETTE ROSSE")); break;
      case 14: display.print(F("CASETTE VERDI")); break;
      case 15: display.print(F("CASETTE BLU")); break;
      case 16: display.print(F("CASETTE WS2811")); break;
      case 17: display.print(F("TUTTO INSIEME")); break;
    }
  } else {
    uint8_t n = testIndice - 18;
    uint8_t gruppo = n / 4 + 1;
    uint8_t rele = n % 4 + 1;
    display.print(F("Grp_"));
    if (gruppo < 10) display.print('0');
    display.print(gruppo);
    display.print('_');
    if (rele < 10) display.print('0');
    display.print(rele);
    if (cfg.nome[n][0]) {            // [RELE] nomeN
      display.setCursor(0,40);
      display.print(cfg.nome[n]);
    }
  }
  display.setCursor(0,48); display.print(F("TEST=avanti START=esci"));
  display.display();
}

void mostraOledRgbPausa(float p) {
  if (!oledPresente) return;
  Fase f = faseDaPercentuale(p);
  int pf = (int)(percentualeFase(p, f) + 0.5f);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print(F("PAUSA"));
  display.setCursor(0,18);
  display.print(nomeFase(f));
  display.print(' ');
  display.print(pf);
  display.print('%');

  display.setCursor(0,30);
  display.print(F("C "));
  display.print(rgbCieloR); display.print(',');
  display.print(rgbCieloG); display.print(',');
  display.print(rgbCieloB);

  display.setCursor(0,41);
  display.print(F("T "));
  display.print(rgbTramontoR); display.print(',');
  display.print(rgbTramontoG); display.print(',');
  display.print(rgbTramontoB);

  display.setCursor(0,52);
  display.print(F("A "));
  display.print(rgbAlbaR); display.print(',');
  display.print(rgbAlbaG); display.print(',');
  display.print(rgbAlbaB);

  display.display();
}

void aggiornaOled(unsigned long durata, float p) {
  if (!oledPresente) return;
  static unsigned long ultimoRefresh=0;
  if (millis()-ultimoRefresh < 120) return;
  ultimoRefresh=millis();

  // In pausa i valori RGB restano visibili stabilmente, così possono essere
  // annotati e riutilizzati per tarare i colori della scenografia.
  if (!running && !testInCorso) {
    mostraOledRgbPausa(p);
    return;
  }

  if (oledPopup != OLED_NESSUNO && (long)(millis()-oledPopupFino)>=0) oledPopup=OLED_NESSUNO;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (oledPopup != OLED_NESSUNO) {
    display.setTextSize(2);
    display.setCursor(8,18);
    switch(oledPopup) {
      case OLED_PAUSA: display.print(F("PAUSA")); break;
      case OLED_RIPRESA: display.print(F("RIPRESA")); break;
      case OLED_AVANTI: display.print(F("AVANTI")); break;
      case OLED_TEST: display.print(F("TEST")); break;
      case OLED_VELOCITA: display.print(F("VELOCITA")); break;
      default: break;
    }
    display.setTextSize(1);
    display.setCursor(8,42);
    if (oledPopup==OLED_VELOCITA) {
      stampaDurataOled(durata);
      display.print(F(" (mm:ss)"));
    } else if (oledPopup==OLED_AVANTI) {
      display.print(F("Fase: ")); display.print(nomeFase(faseDaPercentuale(p)));
    } else if (oledPopup==OLED_TEST) {
      display.print(F("Test uscite in corso"));
    } else {
      display.print(running ? F("Ciclo in esecuzione") : F("Ciclo fermo"));
    }
  } else {
    Fase f=faseDaPercentuale(p);
    int pf=(int)(percentualeFase(p,f)+0.5f);
    display.setTextSize(1);
    display.setCursor(0,0); display.print(F("PSN-PRESEPE"));
    display.setCursor(0,18); display.print(nomeFase(f));
    display.setCursor(94,18); display.print(pf); display.print('%');
    display.drawRect(0,31,128,11,SSD1306_WHITE);
    int fill=(pf*124)/100;
    if(fill>0) display.fillRect(2,33,fill,7,SSD1306_WHITE);
    display.setCursor(0,50);
    display.print(running ? F("RUN ") : F("PAUSA "));
    stampaDurataOled(durata);
    display.print(F(" (mm:ss)"));
  }
  display.display();
}

bool inizializzaOled() {
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000UL, true);
#endif
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission()!=0) return false;
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) return false;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  // Startup splash: PSN-Presepe! by Vanni
  display.setCursor(27,18); display.print(F("PSN-Presepe!"));
  display.setCursor(30,30); display.print(F("by Vanni 037"));
  display.setCursor(18,46);
  stampaDurataOled(durataCicloStabile);
  display.print(F(" (mm:ss)"));
  display.display();
  delay(3000);
  return true;
}

// Esito della lettura microSD, mostrato dopo lo splash per 2,5 s.
void mostraStatoSD() {
  if (!oledPresente) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);  display.print(F("MICRO SD"));
  display.setCursor(0, 16);
  switch (cfg.sdStato) {
    case SD_OK:        display.print(cfgFileLetto); display.print(F(" OK")); break;
    case SD_OK_ERRORI: display.print(F("INI CON ERRORI")); break;
    case SD_NO_FILE:   display.print(F("MANCA PRESEPE.INI")); break;
    default:           display.print(F("SD ASSENTE")); break;
  }
  display.setCursor(0, 28);
  if (cfg.sdStato == SD_OK || cfg.sdStato == SD_OK_ERRORI) {
    display.print(F("Righe ")); display.print(cfg.righeLette);
    display.print(F(" eventi ")); display.print(cfg.numEventi);
  } else {
    display.print(F("Uso valori default"));
  }
  if (cfg.errori) {
    display.setCursor(0, 40);
    display.print(F("Errori ")); display.print(cfg.errori);
    display.setCursor(0, 52);
    if (cfg.primaRigaErrata == 0xFFFF) display.print(F("valori incoerenti"));
    else { display.print(F("prima riga ")); display.print(cfg.primaRigaErrata); }
  }
  display.display();
  delay(cfg.errori ? 5000 : 2500);
}

void stampaConfig() {
  Serial.print(F("microSD: "));
  switch (cfg.sdStato) {
    case SD_OK:        Serial.print(cfgFileLetto); Serial.println(F(" letto senza errori")); break;
    case SD_OK_ERRORI: Serial.print(cfgFileLetto); Serial.print(F(" letto, righe ignorate: ")); Serial.print(cfg.errori);
                       Serial.print(F(" (prima: ")); Serial.print(cfg.primaRigaErrata); Serial.println(F(")")); break;
    case SD_NO_FILE:   Serial.println(F("scheda presente ma PRESEPE.INI / PRESEPE.TXT mancante -> default")); break;
    default:           Serial.println(F("assente o non leggibile -> default")); break;
  }
  Serial.print(F("Scheda inserita (D49): ")); Serial.println(digitalRead(PIN_SD_CD) == LOW ? F("si") : F("no/contatto assente"));
  Serial.print(F("Durate (s): ")); Serial.print(cfg.durataMs[0] / 1000UL); Serial.print('/');
  Serial.print(cfg.durataMs[1] / 1000UL); Serial.print('/'); Serial.println(cfg.durataMs[2] / 1000UL);
  Serial.print(F("Fasi %: ")); Serial.print(cfg.pTramonto); Serial.print(' '); Serial.print(cfg.pCrepuscolo);
  Serial.print(' '); Serial.print(cfg.pNotte); Serial.print(' '); Serial.println(cfg.pAlba);
  {
    static const char *const NOMI_STRISCE[CFG_NUM_STRISCE] = { "CIELO", "TRAMONTO", "ALBA" };
    for (uint8_t s = 0; s < CFG_NUM_STRISCE; s++) {
      Serial.print(F("Tappe ")); Serial.print(NOMI_STRISCE[s]); Serial.print(F(": "));
      Serial.print(cfg.numTappe[s]); Serial.println(cfg.tappeDaFile[s] ? F(" (dal file)") : F(" (predefinite)"));
      for (uint8_t i = 0; i < cfg.numTappe[s]; i++) {
        const CfgTappa &t = cfg.tappe[s][i];
        Serial.print(F("  ")); Serial.print(cfgNomeFase(t.fase)); Serial.print(' '); Serial.print(t.pct);
        Serial.print(F("% -> ")); Serial.print(t.col.r); Serial.print(','); Serial.print(t.col.g); Serial.print(',');
        Serial.print(t.col.b); Serial.println(t.curva == CURVA_LINEARE ? F(" lineare") : F(" morbida"));
      }
    }
  }
  Serial.print(F("Eventi rele': ")); Serial.println(cfg.numEventi);
  for (uint8_t i = 0; i < cfg.numEventi; i++) {
    Serial.print(F("  ")); Serial.print(cfgNomeFase(cfg.eventi[i].fase)); Serial.print(F(" "));
    Serial.print(cfg.eventi[i].pct); Serial.print(F("% rele' ")); Serial.print(cfg.eventi[i].rele + 1);
    if (cfg.nome[cfg.eventi[i].rele][0]) { Serial.print(F(" (")); Serial.print(cfg.nome[cfg.eventi[i].rele]); Serial.print(')'); }
    Serial.println(cfg.eventi[i].acceso ? F(" ON") : F(" OFF"));
  }
  Serial.print(F("Stelle: ")); Serial.print(cfg.numStelle); Serial.print(F(" pixel, attive ")); Serial.println(cfg.stelleAttive);
  Serial.print(F("Casette: ")); Serial.print(cfg.numCasette); Serial.print(F(" pixel, accese da "));
  Serial.print(cfgNomeFase(cfg.accendiFase)); Serial.print(' '); Serial.print(cfg.accendiPct); Serial.print(F("% a "));
  Serial.print(cfgNomeFase(cfg.spegniFase)); Serial.print(' '); Serial.print(cfg.spegniPct); Serial.println('%');
}

void saltaAPercentuale(float p) {
  unsigned long durata = durataCiclo();
  // Arrotonda al millisecondo piu' vicino e entra 1 ms dentro la fase:
  // evita che il troncamento float lasci AVANTI appena prima del confine.
  unsigned long offset = (unsigned long)((durata * (p / 100.0f)) + 0.5f);
  if (p > 0.0f && offset < durata - 1UL) offset++;

  unsigned long riferimento = running ? millis() : pauseStartedMs;
  cycleStartMs = riferimento - offset;
}

void faseAvanti() {
  unsigned long durata = durataCiclo();
  float p = percentualeCiclo(durata);
  Fase nuova = faseSuccessiva(faseDaPercentuale(p));

  saltaAPercentuale(inizioFase(nuova));

  buzzerBeep();
  Serial.print(F("AVANTI -> "));
  Serial.println(nomeFase(nuova));
  oledMostraPopup(OLED_AVANTI);
}

// ============================================================
// SCENA
// ============================================================

void aggiornaScena(float p) {
  Fase fase = faseDaPercentuale(p);

  // Genera una nuova disposizione a ogni ingresso nel crepuscolo.
  if (fase == CREPUSCOLO && ultimaFaseStelle != CREPUSCOLO) {
    generaCieloStellato();
  }
  ultimaFaseStelle = fase;

  // Strisce RGB: colore calcolato dalle tappe di [CIELO], [TRAMONTO], [ALBA]
  // (vedi cfgColoreTappe nel blocco CONFIG). Le tappe di default riproducono
  // le curve storiche: le laterali restano a PWM esattamente zero quando inattive.
  CfgRGB cielo = cfgColoreTappe(cfg, CFG_S_CIELO, p);
  CfgRGB tram  = cfgColoreTappe(cfg, CFG_S_TRAMONTO, p);
  CfgRGB alba  = cfgColoreTappe(cfg, CFG_S_ALBA, p);

  // Stelle: compaiono nel CREPUSCOLO (lineare), piene di NOTTE,
  // si dissolvono durante tutta l'ALBA (curva morbida).
  uint8_t livelloStelle = 0;
  switch (fase) {
    case CREPUSCOLO:
      livelloStelle = interpola8(0, cfg.livelloNotte, progresso(p, P_CREPU, P_NOTTE));
      break;
    case NOTTE:
      livelloStelle = cfg.livelloNotte;
      break;
    case ALBA: {
      float t = progresso(p, P_ALBA, 100.0f);
      float tStelle = t * t * (3.0f - 2.0f * t);
      livelloStelle = interpola8(cfg.livelloNotte, 0, tStelle);
      break;
    }
    default:
      livelloStelle = 0;
      break;
  }

  setCielo(cielo.r, cielo.g, cielo.b);
  setTramonto(tram.r, tram.g, tram.b);
  setAlba(alba.r, alba.g, alba.b);
  setStelle(livelloStelle);
}

// ============================================================
// PULSANTI
// ============================================================

bool pulsantePremuto(uint8_t pin,
                     bool &lastRead,
                     bool &stable,
                     unsigned long &lastChange) {

  bool lettura = digitalRead(pin);
  unsigned long now = millis();

  if (lettura != lastRead) {
    lastRead = lettura;
    lastChange = now;
  }

  if ((now - lastChange) >= DEBOUNCE_MS && lettura != stable) {
    stable = lettura;

    if (stable == LOW)
      return true;
  }

  return false;
}

void toggleStartStop() {
  if (testInCorso) {
    tuttoSpento();
    spegniRele();
    testInCorso = false;
    testIndice = 0;

    // Il tempo passato in TEST non deve far avanzare il ciclo.
    if (testEraInMarcia) {
      cycleStartMs += millis() - testPausaMs;
      running = true;
    } else {
      pauseStartedMs = millis();
      running = false;
    }

    Serial.println(F("USCITA MODALITA' TEST"));
    buzzerBeep();
    oledPopup = OLED_NESSUNO;
    float p = percentualeCiclo(durataCiclo());
    aggiornaScena(p);
    casetteLivello = 0.0f;           // le casette ripartono con la dissolvenza
    aggiornaCasette(p);
    aggiornaReleSchedulati(p);
    aggiornaOled(durataCiclo(), p);
    return;
  }

  if (running) {
    pauseStartedMs = millis();
    running = false;
    Serial.println(F("PAUSA"));
    buzzerBeep();
    oledPopup = OLED_NESSUNO;
  } else {
    unsigned long durataPausa = millis() - pauseStartedMs;

    // Sposta l'origine del ciclo per riprendere esattamente
    // dal punto in cui era stato fermato.
    cycleStartMs += durataPausa;

    running = true;
    Serial.println(F("RIPRESA"));
    buzzerBeep();
    oledMostraPopup(OLED_RIPRESA);
  }
}

// ============================================================
// TEST
// ============================================================

void spegniRele() {
  for (uint8_t i = 0; i < 16; i++)
    scriviRele(i, false);
}

void accendiRele(uint8_t indice) {
  spegniRele();
  if (indice < 16)
    scriviRele(indice, true);
}

void applicaTestCorrente() {
  tuttoSpento();
  spegniRele();

  if (testIndice >= 18) {
    uint8_t n = testIndice - 18;
    accendiRele(n);
    uint8_t gruppo = n / 4 + 1;
    uint8_t rele = n % 4 + 1;
    Serial.print(F("TEST "));
    Serial.print(testIndice + 1);
    Serial.print(F("/34 - Grp_0"));
    Serial.print(gruppo);
    Serial.print(F("_0"));
    Serial.print(rele);
    if (cfg.nome[n][0]) { Serial.print(F(" - ")); Serial.print(cfg.nome[n]); }
    Serial.println();
    mostraOledTest();
    return;
  }

  switch (testIndice) {
    case 0:
      setCielo(255, 0, 0);
      Serial.println(F("TEST 1/34 - CIELO ROSSO"));
      break;
    case 1:
      setCielo(0, 255, 0);
      Serial.println(F("TEST 2/34 - CIELO VERDE"));
      break;
    case 2:
      setCielo(0, 0, 255);
      Serial.println(F("TEST 3/34 - CIELO BLU"));
      break;
    case 3:
      setTramonto(255, 0, 0);
      Serial.println(F("TEST 4/34 - TRAMONTO ROSSO"));
      break;
    case 4:
      setTramonto(0, 255, 0);
      Serial.println(F("TEST 5/34 - TRAMONTO VERDE"));
      break;
    case 5:
      setTramonto(0, 0, 255);
      Serial.println(F("TEST 6/34 - TRAMONTO BLU"));
      break;
    case 6:
      setAlba(255, 0, 0);
      Serial.println(F("TEST 7/34 - ALBA ROSSO"));
      break;
    case 7:
      setAlba(0, 255, 0);
      Serial.println(F("TEST 8/34 - ALBA VERDE"));
      break;
    case 8:
      setAlba(0, 0, 255);
      Serial.println(F("TEST 9/34 - ALBA BLU"));
      break;
    case 9:
      // Verifica il canale rosso di tutti i 50 pixel WS2811.
      stelle.clear();
      for (uint16_t i = 0; i < NUM_STELLE; i++)
        stelle.setPixelColor(i, stelle.Color(70, 0, 0));
      stelle.show();
      Serial.println(F("TEST 10/34 - STELLE ROSSE"));
      break;
    case 10:
      // Verifica il canale verde di tutti i 50 pixel WS2811.
      stelle.clear();
      for (uint16_t i = 0; i < NUM_STELLE; i++)
        stelle.setPixelColor(i, stelle.Color(0, 70, 0));
      stelle.show();
      Serial.println(F("TEST 11/34 - STELLE VERDI"));
      break;
    case 11:
      // Verifica il canale blu di tutti i 50 pixel WS2811.
      stelle.clear();
      for (uint16_t i = 0; i < NUM_STELLE; i++)
        stelle.setPixelColor(i, stelle.Color(0, 0, 70));
      stelle.show();
      Serial.println(F("TEST 12/34 - STELLE BLU"));
      break;
    case 12:
      // Test scenografico esistente: tutte le 50 stelle in bianco caldo tenue.
      stelle.clear();
      for (uint16_t i = 0; i < NUM_STELLE; i++)
        stelle.setPixelColor(i, stelle.Color(70, 50, 27));
      stelle.show();
      Serial.println(F("TEST 13/34 - TUTTE LE 50 STELLE"));
      break;
    case 13:
      casette.clear();
      for (uint16_t i = 0; i < NUM_CASETTE; i++)
        casette.setPixelColor(i, casette.Color(70, 0, 0));
      casette.show();
      Serial.println(F("TEST 14/34 - CASETTE ROSSE"));
      break;
    case 14:
      casette.clear();
      for (uint16_t i = 0; i < NUM_CASETTE; i++)
        casette.setPixelColor(i, casette.Color(0, 70, 0));
      casette.show();
      Serial.println(F("TEST 15/34 - CASETTE VERDI"));
      break;
    case 15:
      casette.clear();
      for (uint16_t i = 0; i < NUM_CASETTE; i++)
        casette.setPixelColor(i, casette.Color(0, 0, 70));
      casette.show();
      Serial.println(F("TEST 16/34 - CASETTE BLU"));
      break;
    case 16:
      casette.clear();
      for (uint16_t i = 0; i < NUM_CASETTE; i++)
        casette.setPixelColor(i, casette.Color(70, 50, 27));
      casette.show();
      Serial.println(F("TEST 17/34 - TUTTE LE CASETTE"));
      break;
    case 17:
      setCielo(120, 90, 70);
      setTramonto(180, 50, 8);
      setAlba(180, 95, 30);
      stelle.clear();
      for (uint16_t i = 0; i < NUM_STELLE; i++)
        stelle.setPixelColor(i, stelle.Color(45, 32, 17));
      stelle.show();
      for (uint16_t i = 0; i < NUM_CASETTE; i++) casette.setPixelColor(i, casette.Color(45, 32, 17));
      casette.show();
      Serial.println(F("TEST 18/34 - TUTTO INSIEME"));
      break;
  }

  mostraOledTest();
}

void testUscite() {
  if (!testInCorso) {
    testInCorso = true;
    testIndice = 0;
    testEraInMarcia = running;
    testPausaMs = millis();
    running = false;
    oledPopup = OLED_NESSUNO;

    Serial.println();
    Serial.println(F("=== MODALITA' TEST ==="));
    Serial.println(F("TEST = test successivo, START = esci"));
    buzzerBeep();
  } else {
    testIndice = (testIndice + 1) % 34;
    buzzerBeep();
  }

  applicaTestCorrente();
}

// ============================================================
// DEBUG
// ============================================================

void stampaStato(unsigned long durata, float p) {
  Serial.print(F("Fase: "));
  Serial.print(nomeFase(faseDaPercentuale(p)));

  Serial.print(F(" | "));
  Serial.print(p, 1);
  Serial.print(F("%"));

  Serial.print(F(" | ciclo: "));
  Serial.print(durata / 60000UL);
  Serial.print(F(" min"));

  Serial.print(F(" | A0: "));
  Serial.print(analogRead(PIN_POT));

  Serial.print(F(" | "));
  Serial.println(running ? F("RUN") : F("PAUSA"));
}

// ============================================================
// SEQUENZA DI BOOT / AUTOTEST VISIVO
// ============================================================

const uint8_t BOOT_STEP_COUNT = 5;

// "Astro del ciel" sul buzzer passivo opzionale, fino a "mite agnello Redentor".
// Tempo volutamente più sostenuto rispetto alla rev.014.
// Autotest visivo, progress bar e melodia terminano insieme.
const uint16_t BOOT_MELODY_FREQ[] = {
  392, 440, 392, 330, 392, 440, 392, 330,
  587, 587, 494, 523, 523, 392,
  440, 440, 523, 494, 440, 392, 440, 392, 330,
  440, 440, 523, 494, 440, 392, 440, 392, 330
};
const uint16_t BOOT_MELODY_MS[] = {
  420, 420, 560, 900, 420, 420, 560, 900,
  560, 420, 560, 560, 420, 900,
  420, 420, 560, 420, 420, 560, 420, 420, 900,
  420, 420, 560, 420, 420, 560, 420, 420, 1100
};
const uint8_t BOOT_MELODY_COUNT = sizeof(BOOT_MELODY_FREQ) / sizeof(BOOT_MELODY_FREQ[0]);
const unsigned long BOOT_MELODY_BASE_MS = 17300UL; // somma originale di BOOT_MELODY_MS[]
const unsigned long BOOT_TOTAL_MS = 21625UL; // +25%: 5 scene alla stessa durata visiva di prima
const unsigned long BOOT_STEP_MS = BOOT_TOTAL_MS / BOOT_STEP_COUNT; // 4,325 s per scena
int8_t bootNotaCorrente = -1;

void aggiornaMelodiaBoot(unsigned long elapsedTotale) {
  if (!cfg.melodiaAvvio || !cfg.buzzer) return;   // [SISTEMA] melodia_avvio / buzzer
  unsigned long limite = 0;
  uint8_t nota = BOOT_MELODY_COUNT;
  for (uint8_t i = 0; i < BOOT_MELODY_COUNT; i++) {
    limite += ((unsigned long)BOOT_MELODY_MS[i] * BOOT_TOTAL_MS) / BOOT_MELODY_BASE_MS;
    if (elapsedTotale < limite) {
      nota = i;
      break;
    }
  }

  if (nota >= BOOT_MELODY_COUNT) {
    if (bootNotaCorrente != -1) {
      noTone(PIN_BUZZER);
      bootNotaCorrente = -1;
    }
    return;
  }

  if (bootNotaCorrente != (int8_t)nota) {
    tone(PIN_BUZZER, BOOT_MELODY_FREQ[nota]);
    bootNotaCorrente = nota;
  }
}

void mostraOledBoot(const __FlashStringHelper *fase, uint8_t step, unsigned long elapsedStep) {
  if (!oledPresente) return;

  // Avanzamento complessivo sui 5 passi, sincronizzato alla durata della melodia.
  unsigned long fatto = (unsigned long)step * BOOT_STEP_MS + elapsedStep;
  unsigned long totale = (unsigned long)BOOT_STEP_COUNT * BOOT_STEP_MS;
  uint8_t pct = (uint8_t)min(100UL, (fatto * 100UL) / totale);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("PSN-PRESEPE"));
  display.setCursor(0, 18);
  display.print(F("Inizializzazione"));
  display.setCursor(0, 30);
  display.print(fase);
  display.setCursor(102, 30);
  display.print(pct);
  display.print('%');

  display.drawRect(0, 45, 128, 11, SSD1306_WHITE);
  int fill = (pct * 124) / 100;
  if (fill > 0) display.fillRect(2, 47, fill, 7, SSD1306_WHITE);
  display.display();
}

void attesaBoot(const __FlashStringHelper *fase, uint8_t step) {
  unsigned long start = millis();
  unsigned long elapsed = 0;
  do {
    elapsed = millis() - start;
    if (elapsed > BOOT_STEP_MS) elapsed = BOOT_STEP_MS;
    mostraOledBoot(fase, step, elapsed);
    aggiornaMelodiaBoot((unsigned long)step * BOOT_STEP_MS + elapsed);
    delay(40);
  } while (millis() - start < BOOT_STEP_MS);
}

void eseguiSequenzaBoot() {
  Serial.println(F("BOOT: autotest visivo uscite + melodia buzzer opzionale"));

  tuttoSpento();
  spegniRele();

  // 1/5 - ALBA: primo quarto della melodia.
  setAlba(255, 255, 255);
  attesaBoot(F("ALBA"), 0);
  setAlba(0, 0, 0);

  // 2/5 - CIELO principale: secondo quarto della melodia.
  setCielo(255, 255, 255);
  attesaBoot(F("CIELO"), 1);
  setCielo(0, 0, 0);

  // 3/5 - TRAMONTO: terzo quarto della melodia.
  setTramonto(255, 255, 255);
  attesaBoot(F("TRAMONTO"), 2);
  setTramonto(0, 0, 0);

  // 4/5 - tutte le 50 stelle: ultimo quarto della melodia.
  stelle.clear();
  for (uint16_t i = 0; i < NUM_STELLE; i++)
    stelle.setPixelColor(i, stelle.Color(255, 255, 255));
  stelle.show();
  attesaBoot(F("STELLE"), 3);
  stelle.clear();
  stelle.show();

  // 5/5 - seconda catena WS2811 CASETTE su D8.
  casette.clear();
  for (uint16_t i = 0; i < NUM_CASETTE; i++)
    casette.setPixelColor(i, casette.Color(255, 255, 255));
  casette.show();
  attesaBoot(F("CASETTE"), 4);
  casette.clear();
  casette.show();

  // I cinque passi coprono l'intera melodia: luce, progress bar e musica
  // terminano insieme prima di PRONTO.
  aggiornaMelodiaBoot(BOOT_TOTAL_MS);
  noTone(PIN_BUZZER);
  bootNotaCorrente = -1;

  // Fine autotest: tutto spento prima dell'avvio del normale ciclo GIORNO.
  stelle.clear();
  stelle.show();
  tuttoSpento();
  spegniRele();
  mostraOledBoot(F("PRONTO"), BOOT_STEP_COUNT, 0);
  delay(900);

  Serial.println(F("BOOT: completato"));
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  // 1) Parametri: default + microSD (/PRESEPE.INI). Va fatto PRIMA di
  //    configurare le uscite, perche' definisce logica rele', lunghezza delle
  //    catene WS2811, ecc. Fino ad allora le uscite restano in alta impedenza
  //    (gate MOSFET con pull-down 100k, ingressi ULN2803 interni a riposo).
  cfgDefault(cfg);
  pinMode(PIN_SD_CD, INPUT_PULLUP);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  cfgCaricaDaSD(PIN_SD_CS, PIN_SD_CD);
  stelle.updateLength(cfg.numStelle);
  casette.updateLength(cfg.numCasette);
  for (uint16_t i = 0; i < CFG_MAX_CASETTE; i++) { casettaFuoco[i] = 255; casettaTarget[i] = 255; }

  // Prima mettiamo tutte le uscite in uno stato definito: durante lo splash
  // OLED nessun ingresso MOSFET/rele deve restare flottante.
  pinMode(PIN_CIELO_R, OUTPUT);
  pinMode(PIN_CIELO_G, OUTPUT);
  pinMode(PIN_CIELO_B, OUTPUT);
  pinMode(PIN_TRAMONTO_R, OUTPUT);
  pinMode(PIN_TRAMONTO_G, OUTPUT);
  pinMode(PIN_TRAMONTO_B, OUTPUT);
  pinMode(PIN_ALBA_R, OUTPUT);
  pinMode(PIN_ALBA_G, OUTPUT);
  pinMode(PIN_ALBA_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);
  stelle.begin();
  stelle.clear();
  stelle.show();
  casette.begin();
  casette.clear();
  casette.show();

  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_NEXT,  INPUT_PULLUP);
  pinMode(PIN_TEST,  INPUT_PULLUP);

  // Inizializza le 16 uscite relè in stato spento.
  // Sono usate sia dalla schedulazione scenografica sia dal test manuale.
  for (uint8_t i = 0; i < 16; i++) {
    digitalWrite(PIN_RELE[i], RELE_ACTIVE_LOW ? HIGH : LOW);
    pinMode(PIN_RELE[i], OUTPUT);
  }

  tuttoSpento();
  spegniRele();

  // Il potenziometro viene letto sempre, anche quando l'OLED non e' presente.
  potRawStabile = potLimita(analogRead(PIN_POT));
  durataCicloStabile = durataDaRaw(potRawStabile);

  oledPresente = cfg.oled ? inizializzaOled() : false;   // [SISTEMA] oled
  mostraStatoSD();
  randomSeed(analogRead(A15) ^ micros());

  // Autotest di accensione: ALBA -> GIORNO -> TRAMONTO -> STELLE -> CASETTE.
  // Cinque passi sincronizzati con la melodia; OLED mostra la progress bar complessiva.
  // [SISTEMA] autotest_avvio = 0 lo salta.
  if (cfg.autotestAvvio) eseguiSequenzaBoot();

  // Rileggi dopo l'autotest: se il potenziometro e' stato mosso
  // durante il boot, il ciclo parte gia' nello scaglione corretto.
  potRawStabile = potLimita(analogRead(PIN_POT));
  durataCicloStabile = durataDaRaw(potRawStabile);

  // Il tempo dell'autotest non fa parte del ciclo scenografico.
  cycleStartMs = millis();
  if (cfg.partenzaInPausa) {            // [CICLO] partenza = pausa
    running = false;
    pauseStartedMs = cycleStartMs;
  }

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F(" PRESEPE CONTROLLER - Rev D (parametri da microSD)"));
  Serial.println(F("========================================"));
  Serial.println(F("D2  = RGB Rosso"));
  Serial.println(F("D3  = RGB Verde"));
  Serial.println(F("D4  = RGB Blu"));
  Serial.println(F("D5  = DATA WS2811 (50 stelle)"));
  Serial.println(F("D8 = DATA WS2811 CASETTE"));
  Serial.println(F("D6  = BUZZER passivo opzionale"));
  Serial.println(F("D7/D11/D12 = RGB SINISTRA / TRAMONTO"));
  Serial.println(F("D44/D45/D46 = RGB DESTRA / ALBA"));
  Serial.println(F("D22 = START/STOP"));
  Serial.println(F("D23 = AVANTI"));
  Serial.println(F("D24 = TEST"));
  Serial.println(F("D20/D21 = OLED I2C 0x3C (opzionale)"));
  Serial.println(oledPresente ? F("OLED: OK") : F("OLED: non presente, continuo senza display"));
  Serial.println(F("A0  = DURATA CICLO (3 scaglioni da [CICLO])"));
  Serial.println(F("D50-D53 = microSD SPI (CS D53), D49 = scheda inserita"));
  stampaConfig();
  Serial.print(F("Durata ciclo impostata all\'avvio: "));
  Serial.print(durataCiclo() / 1000UL);
  Serial.println(F(" secondi"));
  Serial.print(F("Posizione potenziometro A0: "));
  Serial.println(analogRead(PIN_POT));
  Serial.println();
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  buzzerTick();

  // ----- comandi fisici -----

  if (pulsantePremuto(PIN_START,
                      lastStartRead, stableStart, dbStartMs)) {
    toggleStartStop();
  }

  if (pulsantePremuto(PIN_NEXT,
                      lastNextRead, stableNext, dbNextMs)) {
    if (!testInCorso) faseAvanti();
  }

  if (pulsantePremuto(PIN_TEST,
                      lastTestRead, stableTest, dbTestMs)) {
    testUscite();
  }

  // ----- ciclo scenografico -----

  if (!testInCorso) {
    unsigned long durata = durataCiclo();
    float p = percentualeCiclo(durata);

    int potNow = analogRead(PIN_POT);
    int potNowLimitato = potLimita(potNow);

    // Attendi la stabilizzazione prima di cambiare scaglione.
    // Anche uno spostamento minimo oltre una soglia deve essere rilevato.
    if (ultimoPotOled < 0) {
      ultimoPotOled = potRawStabile;
      potPopupUltimaLettura = potNowLimitato;
    }
    if (abs(potNowLimitato - potPopupUltimaLettura) >= POT_BEEP_STABILITA_DELTA) {
      potPopupUltimaLettura = potNowLimitato;
      potPopupUltimaVariazioneMs = millis();
    }
    // Non usare una soglia RAW fissa: impedirebbe di riconoscere
    // il passaggio tra due scaglioni vicino al loro confine.
    potPopupInAttesa = (durataDaRaw(potNowLimitato) != durataCicloStabile);

    if (potPopupInAttesa &&
        millis() - potPopupUltimaVariazioneMs >= POT_POPUP_SETTLE_MS) {
      potPopupInAttesa = false;
      // Mantieni la stessa posizione percentuale della scena quando cambia
      // la durata totale: cambiare velocita' non deve far saltare fase.
      unsigned long vecchiaDurata = durataCicloStabile;
      unsigned long riferimento = running ? millis() : pauseStartedMs;
      unsigned long elapsedVecchio = (riferimento - cycleStartMs) % vecchiaDurata;
      float posizione = (float)elapsedVecchio / (float)vecchiaDurata;

      potRawStabile = potNowLimitato;
      durataCicloStabile = durataDaRaw(potRawStabile);
      unsigned long nuovoElapsed = (unsigned long)(posizione * durataCicloStabile + 0.5f);
      cycleStartMs = riferimento - nuovoElapsed;
      ultimoPotOled = potRawStabile;
      potRawOled = potRawStabile;
      oledMostraPopup(OLED_VELOCITA);
    }

    buzzerPotTick(potNowLimitato);

    // Se lo scaglione e' cambiato, usa subito la durata e la
    // posizione ricalcolate anche per scena, rele' e display.
    durata = durataCiclo();
    p = percentualeCiclo(durata);
    aggiornaScena(p);
    aggiornaCasette(p);
    aggiornaReleSchedulati(p);
    aggiornaOled(durata, p);

    // ----- diagnostica -----
    if (DEBUG_INTERVAL_MS && millis() - lastDebugMs >= DEBUG_INTERVAL_MS) {
      lastDebugMs = millis();
      stampaStato(durata, p);
    }
  }
}
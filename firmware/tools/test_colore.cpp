// Host-side simulation of the "modalita' colore" (colour-picker mode), no Arduino needed.
// Run with firmware/tools/run_tests.sh, which extracts from PSN-Presepe.ino:
//   cfgblock.h  = the CONFIG BEGIN/END block
//   tasto.h     = struct TastoColore / enum EventoTasto
//   mode.inc    = the MODALITA' COLORE section (up to "void setup() {")
// Buttons, potentiometer, clock, OLED and strip outputs are mocked below; the test
// drives a full session: entry with TEST held, strip choice, pick-up, fine mode, exit.
#include <cstdio>
#include <cstdint>
#include <string>
#include <cstring>
struct __FlashStringHelper; 
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
const bool LOW=false, HIGH=true;
unsigned long T=0; unsigned long millis(){return T;}
bool pins[100]; int potRaw=512;
bool digitalRead(int p){return pins[p];}
int analogRead(int){return potRaw;}
const uint8_t PIN_START=22,PIN_NEXT=23,PIN_TEST=24,PIN_POT=54; const unsigned long DEBOUNCE_MS=20;
#include "cfgblock.h"   // CONFIG block of the sketch
#include "tasto.h"
std::string screen, line;
struct Disp{ void clearDisplay(){screen.clear();} void setTextColor(int){} void setTextSize(int){} void setCursor(int,int){screen+="\n";}
 void print(const __FlashStringHelper*s){screen+=(const char*)s;} void print(const char*s){screen+=s;} void print(char c){screen+=c;} void print(int v){screen+=std::to_string(v);} void display(){} } display;
const int SSD1306_WHITE=1; bool oledPresente=true;
struct Ser{ void print(const __FlashStringHelper*s){printf("%s",(const char*)s);} void print(int v){printf("%d",v);} void println(const __FlashStringHelper*s){printf("%s\n",(const char*)s);} void println(int v){printf("%d\n",v);} } Serial;
int beeps=0; void buzzerBeep(bool=true){beeps++;}
uint8_t out[3][3]; void setCielo(uint8_t r,uint8_t g,uint8_t b){out[0][0]=r;out[0][1]=g;out[0][2]=b;} void setTramonto(uint8_t r,uint8_t g,uint8_t b){out[1][0]=r;out[1][1]=g;out[1][2]=b;} void setAlba(uint8_t r,uint8_t g,uint8_t b){out[2][0]=r;out[2][1]=g;out[2][2]=b;}
void tuttoSpento(){memset(out,0,sizeof out);} void spegniRele(){}
bool lastStartRead,stableStart,lastNextRead,stableNext,lastTestRead,stableTest; unsigned long cycleStartMs,pauseStartedMs; bool running;
#include "mode.inc"
int fails=0;
#define CHECK(c) do{ if(!(c)){fails++; printf("FAIL line %d: %s\n",__LINE__,#c);} }while(0)
void run(unsigned long ms){ for(unsigned long i=0;i<ms;i+=5){ T+=5; if(modalitaColore) loopModalitaColore(); } }
void press(int pin, unsigned long hold){ pins[pin]=LOW; run(hold); pins[pin]=HIGH; run(60); }
int main(){
  for(int i=0;i<100;i++) pins[i]=HIGH;
  cfgDefault(cfg); cfg.potInvertito=0; cfg.partenzaInPausa=0;
  pins[PIN_TEST]=LOW;                 // TEST held at power-on
  entraModalitaColore(); run(500);
  CHECK(modalitaColore && coloreStato==0);
  CHECK(out[2][0]==255 && out[0][0]==0);          // ALBA white, others off
  pins[PIN_TEST]=HIGH; run(100);                   // release of the entry press: no event
  CHECK(modalitaColore && coloreStato==0);
  press(PIN_NEXT,100); CHECK(ORDINE_STRISCE_COLORE[coloreStriscia]==CFG_S_CIELO && out[0][1]==255 && out[2][0]==0);
  press(PIN_NEXT,100); CHECK(ORDINE_STRISCE_COLORE[coloreStriscia]==CFG_S_TRAMONTO);
  press(PIN_NEXT,100); CHECK(ORDINE_STRISCE_COLORE[coloreStriscia]==CFG_S_ALBA);
  press(PIN_NEXT,100); press(PIN_START,100);        // CIELO, confirm
  CHECK(coloreStato==1 && coloreCanale==0);
  CHECK(out[0][0]==210 && out[0][1]==82 && out[0][2]==18);   // first CIELO tappa
  // pot at 512 -> 128, R is 210: not engaged
  potRaw=512; run(200); CHECK(!coloreAggancio.agganciato && coloreValori[CFG_S_CIELO][0]==210);
  CHECK(screen.find("gira + verso 210")!=std::string::npos);
  potRaw=840; run(100); potRaw=845; run(100);       // 840*255/1023=209.4 -> 209 within 1 -> engaged
  CHECK(coloreAggancio.agganciato);
  potRaw=1023; run(200); CHECK(coloreValori[CFG_S_CIELO][0]==255 && out[0][0]==255);
  // switch to G: must not jump to the pot value
  press(PIN_NEXT,100); CHECK(coloreCanale==1 && !coloreAggancio.agganciato && coloreValori[CFG_S_CIELO][1]==82);
  potRaw=0; run(100); potRaw=400; run(100);        // 0 -> 100: crosses 82 -> engaged, G follows
  CHECK(coloreAggancio.agganciato && coloreValori[CFG_S_CIELO][1]==100);
  // double click on G within 1 s -> fine mode around 100
  run(1100); press(PIN_NEXT,80); run(300); press(PIN_NEXT,80);   // first: same channel (record), second within 1 s: toggle
  CHECK(coloreFine==1 && coloreBaseFine==100 && !coloreAggancio.agganciato);
  potRaw=512; run(100); CHECK(coloreAggancio.agganciato);   // centre = base
  potRaw=1023; run(100); CHECK(coloreValori[CFG_S_CIELO][1]==116);
  potRaw=0; run(100); CHECK(coloreValori[CFG_S_CIELO][1]==84);
  // two clicks more than 1 s apart do not toggle
  run(1100); press(PIN_NEXT,80); run(1200); press(PIN_NEXT,80); CHECK(coloreFine==1);   // 2 clicks > 1 s apart: no toggle
  // B via TEST short
  press(PIN_TEST,100); CHECK(coloreCanale==2 && coloreFine==0);
  // START long -> back to strip choice, values remembered
  press(PIN_START,2100); CHECK(coloreStato==0 && coloreValori[CFG_S_CIELO][0]==255);
  // TEST long -> exit; TEST still held must not count in the normal loop
  pins[PIN_TEST]=LOW; run(2100); CHECK(!modalitaColore && running && stableTest==LOW && lastTestRead==LOW);
  printf("colour-mode simulation: %s (%d failures)\n", fails?"FAILED":"OK", fails); return fails?1:0;
}

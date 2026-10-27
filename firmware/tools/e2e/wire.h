// Shared "wire" between the ESP8266 side and the Mega side of the end-to-end test.
#pragma once
#include <deque>
extern unsigned long T;                 // common clock, ms
extern std::deque<int> espToMega, megaToEsp;
extern double perdita;                  // fraction of ESP -> Mega bytes lost (WS2811 interrupts off)
extern long persi;                      // bytes actually dropped
bool perso();                           // true = drop this byte
void megaStep();                        // one Mega loop (lnkTick)

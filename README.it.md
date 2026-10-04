# PSN-Presepe — Rev D

*[English](README.md) · Italiano*

Centralina per Arduino Mega 2560 per un presepe modulare a 12 V.

Gestisce un ciclo giorno/notte: **GIORNO → TRAMONTO → CREPUSCOLO → NOTTE → ALBA**. Il ciclo comanda:

- tre strisce RGB analogiche a 12 V: cielo principale, lato tramonto e lato alba;
- due catene WS2811 indirizzabili: stelle che scintillano e finestre delle casette;
- 16 relè per motorini, pompe e lampade.

Tutti i parametri della scenografia si possono cambiare con il file `PRESEPE.INI` su una microSD, letto all'accensione.

Questo repository contiene il progetto Rev D completo:

- il PCB (KiCad), con gli script che lo generano e lo verificano;
- i file di produzione pronti per un assemblatore chiavi in mano;
- il firmware, con i suoi test;
- la documentazione dei collegamenti per realizzare la centralina su breadboard, senza PCB.

> **Stato (2026-10-04).** Il PCB Rev D ha superato tutti i controlli automatici descritti sotto. È stato ordinato a PCBWay con montaggio chiavi in mano (vedi [manufacturing/pcbway-order-2026-10-04](manufacturing/pcbway-order-2026-10-04/README.md)). La scheda fisica **non è ancora stata provata**. Il firmware parte dalla versione già provata su breadboard, con in più la nuova configurazione da SD.

![Rev D lato superiore](previews/PSN-Presepe-Mega-RevD-gerber-render-top.png)

## Indice

1. [Struttura del repository](#1-struttura-del-repository)
2. [Come funziona la centralina](#2-come-funziona-la-centralina)
3. [La scheda Rev D](#3-la-scheda-rev-d)
4. [Connettori della scheda (piedinatura dei morsetti)](#4-connettori-della-scheda-piedinatura-dei-morsetti)
5. [Mappa dei pin dell'Arduino Mega](#5-mappa-dei-pin-dellarduino-mega)
6. [Alimentazione, correnti e fusibili](#6-alimentazione-correnti-e-fusibili)
7. [Collegamento dei carichi](#7-collegamento-dei-carichi)
8. [Montaggio su breadboard / prototipo senza PCB](#8-montaggio-su-breadboard--prototipo-senza-pcb)
9. [Firmware](#9-firmware)
10. [Configurazione da microSD (PRESEPE.INI)](#10-configurazione-da-microsd-presepeini)
11. [Regole di progetto del PCB e verifiche](#11-regole-di-progetto-del-pcb-e-verifiche)
12. [Ordinare la scheda](#12-ordinare-la-scheda)
13. [Ricostruire tutto](#13-ricostruire-tutto)
14. [Limiti noti e punti aperti](#14-limiti-noti-e-punti-aperti)
15. [Sicurezza](#15-sicurezza)
16. [Storia delle revisioni](#16-storia-delle-revisioni)

---

## 1. Struttura del repository

```
build.sh                        ricostruzione + verifica di PCB e firmware con un solo comando
build/
  scripts/                      generatore del PCB, collegamento al router, DRC, verifiche indipendenti, esportazioni
  data/                         netlist/pad di riferimento e sessione di sbroglio salvata (routing.ses)
  lib/PSN_RevD.pretty/          footprint personalizzati (shield Arduino Mega, foro di attrezzaggio JLC)
kicad/                          progetto KiCad 7: scheda, regole, schema (+PDF), libreria locale dei footprint
manufacturing/                  Gerber, mappe di foratura, BOM, file di posizionamento, istruzioni di montaggio, stampa 1:1
  pcbway-order-2026-10-04/      copia CONGELATA dei file inviati davvero a PCBWay (+ SHA256SUMS, note dell'ordine)
previews/                       immagini della scheda (strati KiCad e rendering fotografico dei Gerber)
reports/                        DRC, verifiche indipendenti di scheda e Gerber, parità schema/PCB, controllo footprint
firmware/
  PSN-Presepe/                  tutto il firmware in un solo sketch (PSN-Presepe.ino), PRESEPE.INI di esempio, HEX compilato, flasher Windows
  tools/                        fw_compile.sh, flash.sh (Linux/macOS), run_tests.sh, test_config.cpp
simulation/                     simulazione Wokwi del cablaggio Rev D (diagramma, chip RGB personalizzato, script del pacchetto)
.github/workflows/firmware.yml  CI: test del parser + compilazione del firmware
```

Tutto il contenuto di `kicad/`, `manufacturing/` (tranne la cartella congelata dell'ordine), `previews/` e `reports/` è generato da `./build.sh`. Modifica gli script, non i file generati (vedi [sezione 13](#13-ricostruire-tutto)).

## 2. Come funziona la centralina

| Elemento | Hardware | Comportamento |
|---|---|---|
| **CIELO** | striscia RGB 12 V ad anodo comune, 3 canali MOSFET PWM | Luce diurna calda, bianco pieno a metà giornata, si spegne di notte, bagliore caldo all'alba |
| **TRAMONTO** (sinistra) | striscia RGB 12 V, 3 canali PWM | Ondata arancio/rossa solo durante il tramonto, altrimenti spenta del tutto |
| **ALBA** (destra) | striscia RGB 12 V, 3 canali PWM | Ondata calda più chiara solo durante l'alba |
| **STELLE** | catena WS2811 12 V (50 pixel di default) | Stelle casuali che compaiono al crepuscolo, scintillano una per una di notte e svaniscono all'alba |
| **CASETTE** | catena WS2811 12 V (50 pixel di default) | Luce calda alle finestre da metà tramonto a metà alba, con un leggero tremolio diverso per ogni casa |
| **Relè 1–16** | Omron G5Q-1, contatti puliti COM/NO/NC | Si accendono e spengono in punti programmabili di ogni fase |
| **Comandi** | pulsanti START/STOP, AVANTI (NEXT), TEST, potenziometro velocità | Pausa/ripresa, salto alla fase successiva, test delle uscite, scelta di 1 fra 3 durate del ciclo |
| **OLED** | SSD1306 0,96" 128×64 I2C | Fase, avanzamento, durata del ciclo, stato SD, passi di test (opzionale) |
| **Buzzer** | buzzer passivo piezo o magnetico | Bip dei tasti e melodia all'avvio (opzionale) |
| **microSD** | slot push-push sul bordo della scheda | `PRESEPE.INI` con tutti i parametri; senza scheda si usano i valori di default |

Inizio delle fasi di default, in % del ciclo: TRAMONTO 40 %, CREPUSCOLO 50 %, NOTTE 55 %, ALBA 85 %. ALBA finisce al 100 %, dove riparte il GIORNO successivo.

Durate del ciclo di default: 1, 3 o 5 minuti, scelte col potenziometro.

## 3. La scheda Rev D

- 300 × 180 mm, 2 strati, FR-4 1,6 mm, **rame 2 oz**, HASL, via coperte dal solder mask.
- La scheda è uno shield: si innesta su un **Arduino Mega 2560 R3**, che sta sotto. Gli header della Mega sono montati sul lato inferiore.
- Tutti i collegamenti esterni passano da **morsetti a vite passo 5,08 mm** sui bordi.
- Un solo ingresso a **12 V**. La Mega è alimentata tramite VIN, e la microSD ha un suo regolatore a 3,3 V.

| Blocco | Componenti |
|---|---|
| Ingresso 12 V | J1, condensatore di filtro C1 470 µF |
| Driver RGB | 9 MOSFET logic-level IRLZ44N (Q1–Q9), ognuno con resistenza di gate da 100 Ω e pull-down da 100 kΩ, commutazione sul lato negativo |
| Driver dei relè | 2 × ULN2803A (U1, U2) con diodi di ricircolo interni (pin COM a +12 V), 16 × Omron G5Q-1 DC12 |
| Uscite relè | JR1–JR16, 3 poli COM / NO / NC, zona isolata per 230 V AC (vedi [sezione 11](#11-regole-di-progetto-del-pcb-e-verifiche)) |
| microSD | Hirose DM3AT-SF-PEJM5 push-push con rilevamento scheda, regolatore AMS1117-3.3 da +12 V, traslatore di livello 74LVC125A per SCK/MOSI/CS, pull-up da 10 kΩ |
| Buzzer | BZ1 (passivo, passo 7,6 mm) tramite 220 Ω da D6 |
| Comandi / OLED / potenziometro | Solo morsetti; i componenti stanno fuori dalla scheda |

| Lato superiore (rame + serigrafia) | Lato inferiore (rame + serigrafia) |
|---|---|
| ![sopra](previews/PSN-Presepe-Mega-RevD-top.png) | ![sotto](previews/PSN-Presepe-Mega-RevD-bottom.png) |

Disegno di montaggio: [previews/PSN-Presepe-Mega-RevD-assembly.png](previews/PSN-Presepe-Mega-RevD-assembly.png). Schema: [kicad/PSN-Presepe-Mega-RevD-schematic.pdf](kicad/PSN-Presepe-Mega-RevD-schematic.pdf). Lo schema è generato dalla scheda e usa etichette di rete globali, quindi va letto insieme alle tabelle qui sotto.

## 4. Connettori della scheda (piedinatura dei morsetti)

Il pin 1 è la piazzola quadrata. La funzione di ogni pin è stampata anche sulla serigrafia accanto a ogni morsetto.

### Bordo inferiore, da sinistra a destra

| Morsetto | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Uso |
|---|---|---|---|---|---|
| **J1** POWER 12V | +12 V | GND | | | Ingresso alimentatore |
| **J_OLED** | +5 V (Mega) | GND | SDA (D20) | SCL (D21) | Display OLED |
| **J_START** | D22 | GND | | | Pulsante START/STOP (normalmente aperto) |
| **J_NEXT** | D23 | GND | | | Pulsante AVANTI (normalmente aperto) |
| **J_TEST** | D24 | GND | | | Pulsante TEST (normalmente aperto) |
| **J_POT** | +5 V (Mega) | A0 (cursore) | GND | | Potenziometro esterno lineare da 10 kΩ |
| **J_CIELO** | +12 V | R− | G− | B− | Striscia RGB del cielo (anodo comune) |
| **J_TRAMONTO** | +12 V | R− | G− | B− | Striscia RGB del tramonto |
| **J_ALBA** | +12 V | R− | G− | B− | Striscia RGB dell'alba |
| **J_STELLE** | +12 V | GND | DATA (D5) | | Stelle WS2811 |
| **J_CASETTE** | +12 V | GND | DATA (D8) | | Casette WS2811 |

### Bordi superiore e destro (zona relè)

| Morsetto | Pin 1 | Pin 2 | Pin 3 |
|---|---|---|---|
| **JR1 … JR16** | COM | NO (chiuso a relè acceso) | NC (chiuso a relè spento) |

JR1–JR8 stanno lungo il bordo superiore e JR9–JR16 lungo il bordo destro. Il relè *n* è comandato dal pin D(24+*n*) della Mega: relè 1 = D25 … relè 16 = D40.

**La microSD (J_SD)** è sul bordo superiore, a x ≈ 95 mm. La scheda si inserisce dal bordo e, una volta dentro, sporge di circa 4–5 mm: in una scatola serve una fessura in quel punto.

## 5. Mappa dei pin dell'Arduino Mega

| Pin Mega | Funzione | Note |
|---|---|---|
| D2, D3, D4 | CIELO R, G, B (PWM) | Q1–Q3. D4 usa il Timer0, quindi solo PWM a 8 bit |
| D5 | Dati WS2811 STELLE | |
| D6 | Buzzer | tramite RBZ1 da 220 Ω |
| D7, D11, D12 | TRAMONTO R, G, B (PWM) | Q4–Q6, timer a 16 bit |
| D8 | Dati WS2811 CASETTE | |
| D20 / D21 | I2C SDA / SCL | OLED all'indirizzo 0x3C |
| D22, D23, D24 | START, AVANTI, TEST | `INPUT_PULLUP`; il pulsante chiude il pin verso GND |
| D25 … D40 | Relè 1 … 16 | HIGH = relè acceso (tramite ULN2803A) |
| D44, D45, D46 | ALBA R, G, B (PWM) | Q7–Q9, timer a 16 bit |
| D49 | Rilevamento microSD | LOW = scheda inserita (mostrato solo nel log seriale) |
| D50 / D51 / D52 / D53 | microSD MISO / MOSI / SCK / CS | SPI hardware |
| A0 | Potenziometro velocità | |
| VIN | +12 V dalla scheda | la Mega è alimentata dallo shield |
| 5V | solo verso J_OLED / J_POT | |

Pin liberi: D9, D10, D13, D14–D19, D41–D43, D47, D48, A1–A15. D9, D10 e D13 possono fare anche PWM.

## 6. Alimentazione, correnti e fusibili

- **Alimentatore:** un unico alimentatore switching a 12 V DC. Va dimensionato sulla somma di:
  - strisce RGB al bianco pieno;
  - catene WS2811 (fino a circa 0,7 W per pixel al bianco pieno per le comuni stringhe WS2811 a 12 V; verifica le tue);
  - bobine dei relè (tutti e 16 accesi: meno di 0,6 A);
  - Mega con OLED (circa 0,1 A).
- **Fusibile** sull'uscita a 12 V dell'alimentatore: **non più di 8 A**. Le piste principali +12 V / GND da 3 mm, 2 oz, portano circa 9 A con 10 °C di riscaldamento.
- **Canali RGB:** ogni canale colore è una pista da 1,5 mm, 2 oz, attraverso un IRLZ44N. Tieni ogni canale entro **4 A**. Una tipica striscia RGB 5050 da 1 m assorbe circa 0,4 A per colore.
- **Contatti dei relè:** 10 A / 250 V AC su carico resistivo, secondo i dati del relè. Le piste dei contatti sono doppie, una per strato (2 mm, 2 oz ciascuna), quindi il limite è il relè e non il rame.
  - Motori e altri carichi induttivi hanno un picco di corrente all'accensione: tieni un buon margine.
  - Carichi in corrente continua oltre circa 30 V sono sconsigliati, perché in DC questi contatti faticano a spegnere l'arco.
- **Carichi di segnale:** il G5Q-1 ha contatti di potenza in lega d'argento, fatti per commutare ampere. Correnti molto piccole, come gli ingressi logici (pochi mA), non vengono commutate in modo affidabile. Per quelle usa un relè di segnale con contatti dorati; il carico minimo è nel datasheet Omron.
- **Alimentazione della Mega:** la Mega riduce i 12 V di VIN a 5 V. Quei 5 V alimentano solo OLED e potenziometro, quindi il suo regolatore resta freddo. **Non collegare mai i +12 V al pin 5V della Mega o a un qualsiasi pin di I/O.**
- **USB e 12 V insieme vanno bene:** la Mega sceglie da sola la sorgente di alimentazione. Con la sola USB funzionano Mega, OLED e potenziometro. Relè, strisce e microSD restano senza alimentazione, perché dipendono dai 12 V.

## 7. Collegamento dei carichi

### Strisce RGB analogiche (CIELO / TRAMONTO / ALBA)

Le strisce sono **ad anodo comune** (+12 V in comune). La scheda commuta i ritorni negativi di R, G e B.

```
J_CIELO pin 1 (+12 V) ──► striscia +12V (comune)
J_CIELO pin 2 (R−)    ──► striscia R
J_CIELO pin 3 (G−)    ──► striscia G
J_CIELO pin 4 (B−)    ──► striscia B
```

Non collegare R/G/B di una striscia direttamente a +12 V o a GND.

### Catene WS2811 (STELLE / CASETTE)

```
J_STELLE pin 1 (+12 V) ──► catena +12V
J_STELLE pin 2 (GND)   ──► catena GND
J_STELLE pin 3 (DATA)  ──[330 Ω, consigliata, all'ingresso della catena]──► catena DIN
```

Sulla scheda non c'è una resistenza in serie al segnale DATA. Metti una resistenza da 330–470 Ω nel filo dati, vicino al primo pixel; conta soprattutto con fili lunghi.

Per catene lunghe porta i +12 V anche all'altra estremità. Il numero di pixel si imposta con `[STELLE] numero` e `[CASETTE] numero`, fino a 100 per catena.

### Relè (contatti puliti)

Ogni relè è un contatto in scambio indipendente, quindi ogni relè può commutare un circuito diverso. Per esempio, il relè 1 può commutare 230 V AC mentre il relè 2 comanda una pompa a 12 V.

All'interno dello stesso relè, COM, NO e NC devono appartenere allo **stesso** circuito.

Esempio: un motorino a 230 V AC sul relè 1.

```
Rete L (fase, marrone)       ──► JR1 pin 1 COM
JR1 pin 2 NO                 ──► motorino, filo 1
Rete N (neutro, blu)         ──► motorino, filo 2     (non passa dalla scheda)
Rete PE (terra, giallo-verde) ─► terra del motorino, se c'è (non passa dalla scheda)
```

Se l'Arduino si resetta o fa cose strane quando un motore si spegne, metti un soppressore RC in parallelo al motore: 100 Ω + 100 nF, di classe X2 per 250 V AC.

### Pulsanti, potenziometro, OLED

- **Pulsanti:** pulsanti normalmente aperti fra il pin Dxx di ogni morsetto e il GND accanto. Non servono resistenze.
- **Potenziometro:** 10 kΩ lineare (B10K). Un estremo al pin 1 (+5 V), il cursore al pin 2 (A0), l'altro estremo al pin 3 (GND). Se la manopola funziona "al contrario", imposta `[CICLO] pot_invertito`.
- **OLED:** modulo SSD1306 128×64 I2C all'indirizzo 0x3C, collegato 5 V / GND / SDA / SCL.
  - L'ordine dei pin cambia da modulo a modulo: alcuni sono VCC-GND-SCL-SDA, altri GND-VCC-SCL-SDA. Controlla le scritte stampate sul tuo.
  - Tieni il cavo I2C sotto circa 1 m.

## 8. Montaggio su breadboard / prototipo senza PCB

Ogni blocco del PCB si può rifare su breadboard o millefori usando gli stessi pin della Mega, così lo stesso firmware funziona senza modifiche. Regole comuni:

- Un alimentatore a 12 V per i carichi.
- La Mega si alimenta dall'USB o dai 12 V su VIN.
- **Il GND dell'Arduino e lo 0 V dell'alimentatore a 12 V vanno collegati insieme.** La massa comune serve ai dati delle WS2811 e ai gate dei MOSFET.

### 8.1 Un canale colore RGB (da ripetere 9 volte)

Pin: D2/D3/D4 per CIELO, D7/D11/D12 per TRAMONTO, D44/D45/D46 per ALBA.

```
Mega Dx ──[100 Ω]──┬──► Gate   IRLZ44N  (piedinatura TO-220: G - D - S, visto di fronte)
                   │
                [100 kΩ]
                   │
GND ───────────────┴──► Source
Drain ───────────────► filo colore della striscia (R−, G− o B−)
+12 V ───────────────► +12V comune della striscia
```

In alternativa puoi usare moduli MOSFET già pronti. Se un modulo è attivo basso, imposta `[COLORI] pwm_invertito = 1`.

### 8.2 Relè

Sul PCB un ULN2803A assorbe la corrente delle bobine, e i suoi diodi interni assorbono il picco di tensione quando la bobina si spegne:

```
Mega D25..D32 ──► ULN2803A #1 ingressi 1..8 (pin 1..8)     Mega D33..D40 ──► ULN2803A #2 ingressi 1..8
ULN2803A pin 9 = GND, pin 10 (COM) = +12 V
ULN2803A uscita n (pin 19-n) ──► bobina relè −        bobina relè + ──► +12 V
```

Con moduli relè del commercio da 4 o 8 canali, collega IN1…IN16 a D25…D40. La maggior parte di questi moduli è **attiva bassa**: imposta `[RELE] logica = bassa` in `PRESEPE.INI`. Alimenta le bobine dei moduli con un alimentatore adatto, non dal pin 5 V della Mega.

### 8.3 Catene WS2811

```
Mega D5 ──[330 Ω]──► STELLE DIN        Mega D8 ──[330 Ω]──► CASETTE DIN
alimentatore 12 V + / 0 V ──► catena +12V / GND, con lo 0 V in comune col GND dell'Arduino
```

### 8.4 Comandi, OLED, buzzer

```
D22 ──[START]── GND      D23 ──[AVANTI]── GND      D24 ──[TEST]── GND
+5V ── estremo pot   A0 ── cursore pot   GND ── altro estremo pot
OLED: VCC → 5V, GND → GND, SDA → D20, SCL → D21
D6 ──[220 Ω]──► buzzer passivo + ; buzzer − ──► GND     (opzionale)
```

### 8.5 microSD

Usa un normale modulo "microSD card adapter" a 5 V, del tipo con regolatore a 3,3 V e traslatore di livello. Una basetta solo a 3,3 V senza traslatore si danneggia.

```
modulo VCC → 5V    GND → GND    MISO → D50    MOSI → D51    SCK → D52    CS → D53
```

Il rilevamento scheda (D49) è opzionale: se il modulo non ha il pin CD, lascia D49 scollegato. Il firmware usa CD solo per il log seriale e decide se la scheda c'è provando a leggerla.

## 9. Firmware

Sorgente: **un solo file**, `firmware/PSN-Presepe/PSN-Presepe.ino` (commenti in italiano). Destinazione: Arduino Mega 2560. Lo stesso file si compila con l'IDE Arduino, con `arduino-cli` e in Wokwi. Il lettore di `PRESEPE.INI` sta fra i marcatori `// ==== CONFIG BEGIN ====` e `// ==== CONFIG END ====`; `firmware/tools/run_tests.sh` estrae quel blocco e lo verifica sul PC.

Librerie: Adafruit NeoPixel, Adafruit GFX, Adafruit SSD1306 (con Adafruit BusIO), Arduino SD; SPI e Wire sono incluse nel core. Versioni usate per l'HEX attuale:

| Libreria | Versione |
|---|---|
| NeoPixel | 1.15.5 |
| GFX | 1.12.6 |
| SSD1306 | 2.5.17 |
| BusIO | 1.17.4 |
| SD | 1.3.0 |

Occupazione: 55,9 kB di flash (22 %), 3,7 kB di RAM statica. I buffer dei LED e dell'OLED sono allocati in esecuzione, e restano liberi circa 3 kB.

### Compilazione

- **IDE Arduino:** apri `firmware/PSN-Presepe/PSN-Presepe.ino`, scegli *Arduino Mega or Mega 2560*, poi Carica.
- **Riga di comando:**

  ```sh
  arduino-cli core install arduino:avr
  arduino-cli lib install "Adafruit NeoPixel" "Adafruit GFX Library" "Adafruit SSD1306" "SD"
  firmware/tools/fw_compile.sh                       # -> firmware/PSN-Presepe/build/PSN-Presepe.ino.hex
  firmware/tools/run_tests.sh                        # 54 controlli sul PC del lettore di PRESEPE.INI e delle tappe
  ```

Lo sketch dichiara i suoi prototipi esplicitamente, quindi non dipende dalla generazione automatica dei prototipi dell'IDE.

### Caricare l'HEX già compilato

- **Windows:** metti `PSN-Presepe.ino.hex` e `flash-windows.bat` nella stessa cartella ed esegui il BAT. Al primo avvio scarica arduino-cli e trova da solo la Mega.
- **Linux / macOS:** `firmware/tools/flash.sh [porta]`.

Carica il firmware con l'alimentazione a 12 V spenta: durante il caricamento le uscite cambiano stato, e senza 12 V nessun relè può scattare. La Mega può restare innestata sulla scheda, a patto che la spina USB entri ([sezione 14](#14-limiti-noti-e-punti-aperti)).

### Funzionamento

**Avvio:**
1. Carica i valori di default.
2. Legge `/PRESEPE.INI` dalla microSD, oppure `/PRESEPE.TXT` se il primo manca.
3. Mostra la schermata iniziale sull'OLED.
4. Mostra lo stato della SD (con `fastboot = 1` solo se `PRESEPE.INI` contiene errori):

   | Messaggio OLED | Significato |
   |---|---|
   | `PRESEPE.INI OK` (o `PRESEPE.TXT OK`) | file letto, nessun errore |
   | `INI CON ERRORI` | file letto con errori; indica quanti sono e la prima riga sbagliata |
   | `MANCA PRESEPE.INI` | scheda presente, nessuno dei due file trovato |
   | `SD ASSENTE` | nessuna scheda |

5. Esegue l'autotest (circa 22 s) con melodia opzionale. Saltato con `fastboot = 1`.
6. Avvia il ciclo dall'inizio del GIORNO, in marcia o in pausa secondo `partenza`.

**Avvio rapido** (`[SISTEMA] fastboot`, predefinito **1** dalla release 038): salta l'attesa di 3 s sulla schermata iniziale, la schermata della SD (tranne se il file ha errori), l'autotest e la melodia, così la scena parte subito. Con `fastboot = 0` l'avvio è completo, e `autotest_avvio` e `melodia_avvio` decidono se eseguire autotest e melodia.

**Pulsanti:**

| Pulsante | Azione |
|---|---|
| **START/STOP** | Pausa o ripresa; riparte esattamente da dove si era fermato. In modalità TEST, esce dal TEST. |
| **AVANTI (NEXT)** | Salta all'inizio della fase successiva. |
| **TEST** | Entra in modalità TEST, poi scorre 34 prove: ogni colore di ogni striscia, colori di stelle e casette, tutto insieme, poi i relè 1–16 uno alla volta, con il loro nome se definito. Durante il TEST il tempo del ciclo è fermo. |
| **Potenziometro** | Sceglie una delle tre durate del ciclo; la modifica vale quando la manopola si ferma. |

**Monitor seriale** (115200 baud): all'avvio stampa tutta la configurazione in uso, compresi tutti gli eventi dei relè, poi una riga di stato periodica (`debug_ms`).

### Simulazione

Il firmware gira invariato nel simulatore Wokwi con il cablaggio Rev D: barre RGB, stelle, casette, relè come LED, OLED, pulsanti, potenziometro e microSD. `PSN-Presepe.ino` si incolla così com'è nel `sketch.ino` di Wokwi. Wokwi non accetta file `.ini`, quindi la configurazione va in un file del progetto chiamato `PRESEPE.TXT`, che il firmware legge quando manca `PRESEPE.INI`. Vedi [simulation/README.md](simulation/README.md).

## 10. Configurazione da microSD (PRESEPE.INI)

Copia [`firmware/PSN-Presepe/PRESEPE.INI`](firmware/PSN-Presepe/PRESEPE.INI) nella **radice** di una microSD o microSDHC (2–32 GB, FAT16/FAT32). Le schede da 64 GB in su escono formattate exFAT, che la libreria SD di Arduino non legge: riformattale prima in FAT32.

La scheda viene letta **una sola volta, all'accensione**. Se `PRESEPE.INI` manca, il firmware cerca `PRESEPE.TXT` con lo stesso contenuto (comodo per Wokwi, o se Windows ha salvato il file come `.txt`). Il file di esempio contiene esattamente i valori di default, e ogni chiave è spiegata in italiano nei commenti.

| Sezione | Chiavi |
|---|---|
| `[CICLO]` | `durata1..3` (s, 10–86400), `pot_min`, `pot_max`, `pot_invertito`, `partenza = marcia / pausa` |
| `[FASI]` | % di inizio di `tramonto`, `crepuscolo`, `notte`, `alba` (devono essere strettamente crescenti) |
| `[RELE]` | `logica = alta / bassa`, `nome1..16` (max 12 caratteri), `forza1..16 = auto / on / off`, `evento = FASE, RELÈ, ON/OFF, %` (fino a 64) |
| `[CIELO]`, `[TRAMONTO]`, `[ALBA]` | `tappa = FASE, %, R, G, B [, morbida / lineare]` (fino a 20 per striscia), vedi sotto |
| `[COLORI]` | `lum_cielo / lum_tramonto / lum_alba` (%), `gamma`, `pwm_invertito` |
| `[STELLE]` | `numero` (≤ 100), `attive`, `lum_min / lum_max`, `livello_notte`, `scintillio_min / max` (ms), `colore` (tinta in %) |
| `[CASETTE]` | `numero` (≤ 100, 0 = spente), `colore`, `accendi = FASE, %`, `spegni = FASE, %` (può scavalcare la fine del ciclo), `fuoco` (0–100), `dissolvenza_ms` |
| `[SISTEMA]` | `fastboot` (predefinito 1), `buzzer`, `beep_hz`, `beep_ms`, `melodia_avvio`, `autotest_avvio` (entrambi solo con `fastboot = 0`), `oled`, `debug_ms` |

Gli eventi dei relè restano validi finché non arriva il successivo: un relè mantiene lo stato fino al suo prossimo evento. Un relè si può indicare col numero (1–16), col nome, o come `Grp_GG_RR`, dove relè = (GG − 1) × 4 + RR. La prima riga `evento` valida sostituisce tutta la tabella di default.

Esempio: il motorino del mulino sul relè 1 gira dal 10 % del GIORNO fino a quando arriva la notte.

```ini
[RELE]
nome1  = Mulino
evento = GIORNO, Mulino, ON, 10
evento = NOTTE,  Mulino, OFF, 0
```

### Colori delle strisce RGB: le "tappe"

Il colore di ogni striscia analogica lungo il ciclo è una lista di **tappe**, nella sezione della striscia: `[CIELO]`, `[TRAMONTO]` o `[ALBA]`. Una tappa dice: *in questo punto del ciclo la striscia ha esattamente questo colore*. Fra due tappe consecutive il firmware passa gradualmente da un colore all'altro.

```ini
tappa = FASE, % della fase, R, G, B [, curva]
```

- **FASE, %:** dove si trova la tappa. Per esempio `TRAMONTO, 38` è al 38 % della fase del tramonto. I decimali si scrivono col punto: `33.33`.
- **R, G, B:** il colore in quel punto, da 0 a 255. `0, 0, 0` vuol dire spenta.
- **curva** (facoltativa) indica come la striscia *arriva* a questa tappa dalla precedente:
  - `morbida` (predefinita): parte piano, accelera a metà e rallenta arrivando (smoothstep), la dissolvenza naturale usata finora;
  - `lineare`: velocità costante per tutto il tratto.

Regole:
- Scrivi le tappe in ordine di tempo, dal GIORNO all'ALBA. Una tappa che torna indietro nel tempo viene ignorata e conta come errore.
- La lista fa il giro: dopo l'ultima tappa la striscia va gradualmente verso la prima tappa del ciclo successivo.
- **Due tappe nello stesso punto fanno un cambio istantaneo** (uno scatto): la striscia arriva al primo colore e riparte dal secondo.
- Per tenere una striscia spenta in un tratto, metti una tappa `0, 0, 0` all'inizio e una alla fine di quel tratto.
- Fino a 20 tappe per striscia. Una sola tappa vuol dire colore fisso per tutto il ciclo.
- Se una sezione contiene almeno una tappa valida, le tappe del file sostituiscono **tutte** quelle predefinite di quella striscia. Le strisce senza tappe nel file restano con quelle predefinite.

Le tappe predefinite di TRAMONTO e ALBA riproducono le curve storiche del firmware. La curva diurna del CIELO è stata ridisegnata nella release 038: al 30 % del GIORNO è quasi bianca (80 % della strada fra caldo e bianco), tiene il bianco pieno dal 45 % al 55 %, torna all'80 % al 70 % e rientra nel colore caldo all'inizio del TRAMONTO. Per esempio la striscia del tramonto:

```ini
[TRAMONTO]
tappa = TRAMONTO,   0,   0,  0,  0     ; spenta fino all'inizio del tramonto
tappa = TRAMONTO,   0,  18, 10,  4     ; scatto: si accende tenue e calda
tappa = TRAMONTO,  38, 155, 92, 16     ; picco arancio
tappa = TRAMONTO,  82,  16, 16, 16     ; perde colore fino a un bianco tenue
tappa = TRAMONTO, 100,   0,  0,  0     ; spenta a fine tramonto, fino al giro dopo
```

Per aggiungere un colore intermedio, per esempio un passaggio rosso fra il picco arancio e il bianco tenue, basta inserire una tappa in mezzo: `tappa = TRAMONTO, 60, 140, 30, 10`.

Quando sono state introdotte le tappe (prima della modifica al cielo della release 038), quelle predefinite sono state confrontate con il firmware precedente, che aveva le curve scritte nel codice, in 1.000.000 di punti del ciclo:
- le strisce TRAMONTO e ALBA differiscono al massimo di 1 gradino su 255 (arrotondamenti), in 15 punti su un milione;
- il CIELO differisce di 1 gradino su 255 nello 0,5 % dei punti;
- l'unica differenza maggiore (10 gradini) cade nel solo istante dello "scatto" dell'alba al 38 %, dove il codice vecchio e il nuovo arrotondano il confine in modo diverso.

Niente di tutto questo è visibile.

Gestione degli errori:
- Una riga sbagliata viene saltata e contata, e quella chiave mantiene il valore di default.
- Percentuali delle fasi non crescenti, o un intervallo del potenziometro inutilizzabile, tornano ai valori di default.

## 11. Regole di progetto del PCB e verifiche

### Isolamento dei contatti dei relè (230 V AC)

| Distanza | Regola | Misurata |
|---|---|---|
| Rame dei contatti relè ↔ qualsiasi rame a bassa tensione (SELV) | ≥ 6,0 mm | 6,47 mm |
| Contatti di relè diversi | ≥ 5,0 mm | 5,23 mm |
| COM / NO / NC dello stesso relè | ≥ 2,0 mm | 2,15 mm |
| Contatti dei relè ↔ bordo scheda | ≥ 3,0 mm | rispettata |
| Contatti dei relè ↔ fori non di rete | ≥ 6,0 mm | 8,7 mm |

Non ci sono via sulle reti a tensione di rete.

Le 48 reti dei contatti (COM/NO/NC × 16) collegano ciascuna un solo pin del relè al suo morsetto, quindi i 230 V non raggiungono mai il resto della scheda. Questo viene controllato in automatico.

### Controlli automatici (`reports/`)

| Controllo | Risultato |
|---|---|
| DRC di KiCad con classi di rete e regole di isolamento personalizzate | **0 violazioni, 0 non collegati** |
| Controprove: ogni regola di isolamento forzata a un valore impossibile, un'esecuzione per regola | tutte e 4 le regole scattano, quindi sono attive |
| `verify.py`: verifica geometrica indipendente con shapely, senza KiCad | **tutti i controlli superati** (distanze per coppia di classi, isole di rame, larghezze, netlist rispetto alla Rev B + modifiche documentate, piedinature di relè/ULN/MOSFET, cablaggio microSD rispetto ai pin del firmware) |
| `verify_gerbers.py`: verifica indipendente dei file Gerber X2 / Excellon | **tutti i controlli superati** |
| Schema ↔ PCB (netlist di kicad-cli) | 363 / 363 pin coincidono |
| `check_mega_footprint.py` | 32 / 32 pin e 4 / 4 fori dell'UNO R3 coincidono con il footprint ufficiale di KiCad (0,000 mm); fori e header propri della Mega coincidono con le quote pubblicate della Mega R3 |
| `compare_gerbers.py` rispetto ai file inviati a PCBWay | geometria identica, primitiva per primitiva |

La distanza minima tra rame sulla scheda è 0,202 mm (7,95 mil).

## 12. Ordinare la scheda

### PCBWay (chiavi in mano, usato per il primo ordine)

1. **Assembly → SMT/THT**. Scegli turnkey, single pieces, **both sides** (entrambi i lati).
   - Unique parts: 20; SMD parts: 12; BGA/QFP: 0; through-hole parts: 86.
   - Accetta alternative solo per i passivi.
2. **PCB:**
   - 300 × 180 mm, 2 strati, FR-4, 1,6 mm, 6/6 mil (PCBWay lo porta a 8/8 con 2 oz), foro minimo 0,3 mm;
   - HASL senza piombo, via coperte (tented), **2 oz**, rimozione del codice prodotto.
3. **Caricamento dei file:**

   | Campo | File |
   |---|---|
   | Gerber | `manufacturing/PSN-Presepe-Mega-RevD-gerbers.zip` |
   | BOM | `…-BOM-PCBWay.csv` (codici produttore e note di montaggio) |
   | Centroid | `…-Centroid-PCBWay.csv` (comprende MCU1 sul lato inferiore) |
   | Other files | `…-Assembly-Instructions.pdf` (header Mega sul lato inferiore, fori ICSP vuoti, note sulle polarità) |

4. Attendi la revisione (*audit*) e il DFM. Il costo dei componenti viene quotato solo dopo la revisione.

I file e le impostazioni esatte dell'ordine del 2026-10-04 sono congelati in `manufacturing/pcbway-order-2026-10-04/`.

### JLCPCB (alternativa)

Gerber + `…-BOM-JLCPCB.csv` + `…-CPL-JLCPCB.csv`. Fori di attrezzaggio: *Added by Customer* (TH1–TH3 sono già sulla scheda).

JLCPCB monta un solo lato, quindi **gli header della Mega vanno saldati a mano.**

### Prima di ordinare una nuova revisione

Stampa `…-1to1-A3-top.pdf` al 100 % e verificaci sopra i componenti veri.

## 13. Ricostruire tutto

Requisiti (versioni usate):

| Strumento | Versione | Scopo |
|---|---|---|
| KiCad | 7.0.11 | API Python `pcbnew` e `kicad-cli` |
| Python | 3 | con `shapely`, `gerbonara`, `reportlab`, `Pillow` |
| `rsvg-convert` | | immagini |
| `zip` | | archivio dei Gerber |
| arduino-cli | 1.1.1 | con arduino:avr |
| compilatore C++11 | | test del parser |
| Freerouting 2.2.4 + Java 25 | | solo per rifare lo sbroglio da zero |

```sh
./build.sh                    # tutto in ./out (PCB riprodotto da build/data/routing.ses + firmware)
UPDATE_REPO=1 ./build.sh      # come sopra, poi aggiorna kicad/ manufacturing/ previews/ reports/ nel repo
REROUTE=1 FREEROUTING_JAR=/percorso/freerouting-2.2.4.jar JAVA=/percorso/java ./build.sh   # sbroglio da zero
SKIP_PCB=1 ./build.sh         # solo firmware
```

Come funziona la catena del PCB (`build/scripts/pipeline.sh`):
1. Piazza tutti i footprint a partire dalla netlist di riferimento (`build_board.py`).
2. Scrive le classi di rete e le regole di isolamento (`make_rules.py`).
3. Pre-sbroglia i percorsi di rete, le piste di alimentazione e le zone vietate (`route_mains.py`).
4. Sbroglia con Freerouting, oppure riproduce la sessione salvata.
5. Importa la sessione (`import_ses.py`), completa i collegamenti rimasti con un router A* (`complete_routes.py`), toglie le via di uscita inutilizzate (`cleanup.py`), poi esegue il DRC finale.

`make_release.sh` poi aggiorna i dati di revisione, esporta tutto ed esegue tutte le verifiche indipendenti. `package.sh` dispone il risultato come in questo repository.

La sessione di sbroglio salvata rende la ricostruzione deterministica: una ricostruzione pulita da questo albero produce una geometria identica ai Gerber ordinati. Freerouting invece non è deterministico, quindi rifare lo sbroglio da zero dà un tracciato diverso, ma comunque verificato.

A ogni build KiCad assegna nuovi identificativi interni (`tstamp`) a tutti gli elementi della scheda, quindi in git `kicad/*.kicad_pcb` mostra grandi differenze anche quando non è cambiato nulla. Per giudicare una ricostruzione usa `reports/` (soprattutto `gerber-equivalence-vs-pcbway-order.txt`), non il diff testuale.

KiCad 8–10 aprono i file di KiCad 7, ma gli script sono scritti per l'API Python di KiCad 7.

## 14. Limiti noti e punti aperti

- **Spazio per la spina USB.** La presa USB-B della Mega finisce circa 1,7 mm all'interno del bordo dello shield, e lo shield sta circa 11 mm sopra la Mega. Le spine USB voluminose potrebbero non entrare fino in fondo. In quel caso usa una spina sottile, degli header impilabili, oppure stacca la Mega per programmarla. Sopra la presa USB e il jack di alimentazione non c'è rame sullo shield.
- **Dati WS2811:** sulla scheda manca la resistenza in serie, va aggiunta da 330 Ω nel cavo ([sezione 7](#7-collegamento-dei-carichi)).
- **Blu del CIELO (D4):** è sul Timer0, quindi resta a PWM 8 bit. TRAMONTO e ALBA (e R/G del CIELO) sono su timer a 16 bit e in un firmware futuro potrebbero passare a PWM da 10–12 bit.
- **Distanza minima** di 0,202 mm (7,95 mil), mentre PCBWay indica 8 mil per il rame da 2 oz. La differenza è di 1 µm, molto al di sotto della tolleranza di incisione, ed è stata segnalata a PCBWay con l'ordine.
- **ERC non eseguito.** `kicad-cli` 7 non ha l'ERC; le connessioni sono dimostrate dalla parità schema ↔ PCB e dalle verifiche indipendenti.
- **I Gerber ordinati riportano revisione "C"** nell'attributo di intestazione X2 `ProjectId`, residuo del cartiglio. È solo estetico, non incide sulla produzione ed è corretto negli script. Vedi la cartella congelata dell'ordine.
- **Avvisi di annotazione:** i riferimenti `J_*` fanno segnalare a KiCad avvisi di annotazione. Sono innocui.

## 15. Sicurezza

- La zona relè (bordi superiore e destro) è a **tensione di rete** ogni volta che la rete è collegata a un morsetto, anche con il relè spento.
- Monta la scheda in una scatola chiusa e isolante.
- Proteggi con un fusibile la linea di rete che alimenta i relè.
- Stacca la rete prima di toccare la scheda.
- Tieni separati i cavi di rete da quelli a bassa tensione, con percorsi distinti.
- Prova l'hardware nuovo prima con i contatti dei relè scollegati, oppure con carichi a bassa tensione.
- Tratta qualsiasi circuito a bassa tensione collegato a un relè vicino a uno di rete con la stessa prudenza dei cavi di rete.

## 16. Storia delle revisioni

| Rev | Sintesi |
|---|---|
| A | Prima versione PCB della centralina su breadboard (revisionata, non prodotta). |
| B | Shield per Arduino Mega con relè, MOSFET e morsetti (versione precedente del repository). |
| C | Correzioni per la produzione: pin NO/NC del G5Q-1 corretti, footprint DIP-18 dell'ULN2803A, resistenza in serie al buzzer, isolamenti 6 / 5 / 2 mm, piste di alimentazione da 3 mm, fori di attrezzaggio JLCPCB, suite di verifica completa. |
| D | microSD push-push con regolatore 3,3 V e traslatore di livello; il firmware legge `PRESEPE.INI` all'avvio (con valori di default incorporati); potenziometro di bordo sostituito dal morsetto J_POT; catena CASETTE gestita nel ciclo; file per il montaggio chiavi in mano PCBWay. |

---

Licenza: non ancora scelta. Aggiungi un file `LICENSE` per definire come PSN-Presepe può essere riutilizzato.

Di **Vanni Brutto**

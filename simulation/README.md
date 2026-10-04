# Wokwi simulation — PSN-Presepe Rev D

The simulation runs the **real firmware** on a simulated Arduino Mega. The wiring follows the Rev D PCB, including the microSD card (configuration in `PRESEPE.TXT`) and the CASETTE chain.

Online project: <https://wokwi.com/projects/476977183363259393>

## Updating the Wokwi project

The firmware is a single file, so the Wokwi project needs only these files:

| Wokwi file | Content |
|---|---|
| `sketch.ino` | `firmware/PSN-Presepe/PSN-Presepe.ino`, unchanged (replace everything) |
| `diagram.json` | `simulation/diagram.json` (or `diagram-no-display.json`) |
| `libraries.txt` | `simulation/libraries.txt` (adds `SD`) |
| `PRESEPE.TXT` | `firmware/PSN-Presepe/PRESEPE.INI` — new file (▾ next to the file tabs → *New file…*) |
| `rgb-strip.chip.json`, `rgb-strip.chip.c` | same names in `simulation/`, unchanged |

`simulation/make_wokwi_bundle.sh [out_dir] [--no-display]` copies them into one folder with these names.

Wokwi does not accept `.ini` files, so the configuration goes in `PRESEPE.TXT`. The firmware looks for `PRESEPE.INI` first and falls back to `PRESEPE.TXT`, so the very same sketch runs on the real board and in Wokwi. On the OLED you will see `PRESEPE.TXT OK`.

If an older version of the project has `PresepeConfig.h` or `PresepeConfig.cpp` tabs, delete them. That code is now inside the sketch, and a second copy causes "multiple definition" errors.

**How the SD card works in Wokwi:** when the simulation starts, Wokwi copies every project file onto the simulated microSD card, formatted as FAT16.

To try a different configuration:
1. Edit `PRESEPE.TXT` in Wokwi.
2. Restart the simulation. The file is read only at boot.

Rename the file to check the defaults: the OLED then shows `MANCA PRESEPE.INI`.

## What is simulated

| Real hardware (Rev D PCB) | Simulation | Mega pins |
|---|---|---|
| CIELO RGB strip via MOSFETs | custom `chip-rgb-strip` bar | D2 / D3 / D4 |
| TRAMONTO RGB strip | custom `chip-rgb-strip` bar | D7 / D11 / D12 |
| ALBA RGB strip | custom `chip-rgb-strip` bar | D44 / D45 / D46 |
| STELLE, 12 V WS2811, 50 pixels | 50 × `wokwi-neopixel` (5 V, simulator only) | D5 |
| CASETTE, 12 V WS2811, 50 pixels | `wokwi-led-strip`, 50 pixels | D8 |
| Passive buzzer | `wokwi-buzzer` | D6 |
| OLED SSD1306 128×64, I2C 0x3C | `wokwi-ssd1306` | D20 / D21 |
| START/STOP, NEXT, TEST buttons | 3 push buttons to GND | D22 / D23 / D24 |
| External 10 kΩ potentiometer (J_POT) | `wokwi-potentiometer` | A0 |
| 16 relays G5Q-1 via ULN2803A | 16 LEDs (`Grp_01_01` … `Grp_04_04`), HIGH = on | D25 … D40 |
| microSD push-push slot | `wokwi-microsd-card` | D50 MISO, D51 MOSI, D52 SCK, D53 CS |

Notes:
- **Card detect (D49) is not wired.** In Wokwi the card is always inserted and its CD pin is not driven. The firmware uses card detect only for the serial log ("Scheda inserita (D49): no/contatto assente" in the simulator), so this is expected.
- The simulation does not model voltages, currents, MOSFETs or relay contacts. It checks logic, timing, colours and configuration, not the power electronics.
- The custom RGB bar draws the colour obtained from the PWM duty cycle; black means off. It is a display aid only.

## Variant without OLED

`diagram-no-display.json` is the same circuit without the SSD1306. It checks that the firmware carries on normally without a display.

**Maintenance rule:** every wiring change to `diagram.json`, other than the OLED, must also be made in `diagram-no-display.json`.

## Running the simulation from the command line (optional)

`wokwi.toml` points to the HEX that `firmware/tools/fw_compile.sh` builds. With the Wokwi VS Code extension or `wokwi-cli` (Wokwi token required), run it from the `simulation/` folder. This has not been automated in CI.

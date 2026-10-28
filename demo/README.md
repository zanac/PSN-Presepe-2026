# Demo of the web pages (no hardware needed)

The two pages served by the ESP8266 of a "Mega + WiFi R3" board ([README, WiFi](../README.md#wifi-mega--wifi-r3-board)), running in the browser against a **simulated presepe**:

| Page | On the board | Open online |
|---|---|---|
| `index.html` | `http://presepe.local/`: control panel, cycle commands, web test of strips, stars/houses, relays, buzzer | [open](https://raw.githack.com/zanac/psn-presepe-2026/main/demo/index.html) |
| `config.html` | `http://presepe.local/config`: `PRESEPE.INI` editor with live preview, save, restart, restore | [open](https://raw.githack.com/zanac/psn-presepe-2026/main/demo/config.html) |

Or download the folder and open `index.html` with a double-click. The orange bar on top says it is a demo.

## What the simulated board does

- Starts from the repository's `PRESEPE.INI` and runs the cycle with the same parser and light engine as the [web simulator](../simulation_web/README.md), so phases, strip colours and relays follow the file.
- *Pausa / Avvia*, *Fase successiva*, *Entra in test*, *Stop: spegni tutto* / *Play* and every test command behave like on the board (the buzzer button beeps).
- *Salva sul presepe* writes the "microSD" (kept in the browser, shared by the two pages), with `PRESEPE.BAK`; *Riavvia il presepe* makes the board unreachable for 4 s, like the real restart, then uses the new file; *Ripristina il file precedente* swaps the two files.
- *rimetti la microSD di esempio* (in the orange bar) puts the original file back.

There is no password prompt: on the board the browser asks for the `[AUTENTICAZIONE]` user and password.

## How it is built

Generated, do not edit by hand. `python3 firmware/tools/build_web.py` writes both pages from the real sources, so the demo is always the same code as the firmware pages:

- `firmware/web/pannello.html` and `simulation_web/simulator.template.html` (the pages);
- `firmware/web/demo_mock.js` (the simulated board: answers `/api/*` in the browser);
- `firmware/PSN-Presepe/PRESEPE.INI` (the file on the simulated card).

Run it after changing any of these; `build_web.py --check` (in CI) fails when the demo is out of date.

# Web simulator — PRESEPE.INI

A single HTML page that runs the light cycle described by a `PRESEPE.INI` in the browser, with no hardware and no Arduino. Use it to try curves, colours and timings before copying the file to the microSD card.

Open `index.html` in any modern browser (double-click it). No server and no install needed; only the fonts come from Google Fonts, and the page works with fallback fonts when offline.

## What it shows

- **Left:** the INI text. Edit or paste a file; the simulation updates as you type. Parse errors are listed under the text with the same messages the firmware prints on the serial monitor. *Ripristina il file predefinito* reloads the repo's `PRESEPE.INI`; *Copia il file* copies the text, ready for the microSD.
- **Right:** three panels, TRAMONTO (left), CIELO (centre), ALBA (right), with their current RGB values; the name of the current phase and the position in the cycle.
- **Cycle line:** one band per strip (top to bottom: TRAMONTO, CIELO, ALBA), the tappe as white ticks, the star level at the bottom. Click or drag on it to jump to any point of the cycle.
- **Under the line:** star level, CASETTE on/off, the 16 relays.
- **Transport:** play/pause, cycle duration (`durata1`–`durata3` from the file, plus two fast speeds), previous/next phase.

The text you type is kept in the browser's local storage, so it survives a page reload.

## How faithful it is

The JavaScript re-implements the firmware's pure-logic part (the CONFIG block of `PSN-Presepe.ino`, release 038): the parser and its error rules, the defaults, phase positions, the tappe engine (`morbida` smoothstep and `lineare`, truncating interpolation, wrap-around across the cycle end), stars, casette window and relay schedule.

It is a model, not the firmware: when the firmware logic changes, update `simulator.template.html` too.

A screen is not an LED strip. Real strips are more saturated and low values look different, so use the simulator for shape and timing, then fine-tune colours on the real strip or with the board's colour mode.

## Files

| File | Content |
|---|---|
| `index.html` | the simulator, generated — open this one |
| `simulator.template.html` | source of the page, with an `__INI__` placeholder for the default file |
| `build_simulator.py` | rebuilds `index.html` from the template and `firmware/PSN-Presepe/PRESEPE.INI` |

After every change to `firmware/PSN-Presepe/PRESEPE.INI` or to the template, rebuild:

```sh
python3 simulation_web/build_simulator.py
# options: --ini PATH  --template PATH  --out PATH
```

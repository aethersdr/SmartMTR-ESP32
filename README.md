# AetherKnob

A FlexRadio controller on the **Elecrow CrowPanel 2.1" HMI ESP32 Rotary
Display** (DHE03921D: ESP32-S3R8, 480×480 round IPS touch screen, push knob).
The face is modelled on [SmartMTR](https://smartmtr.net/): S-meter arcs for two
slices, SWR/COMP/PWR/MIC needles, and the frequency readout, with a dock of touch
tiles in the middle.

It plugs into the PC running AetherSDR by USB. AetherSDR's **CTR2 Proxy**
applet relays it to the radio over the CTR2 USB link v0
([spec](https://github.com/aethersdr/AetherSDR/blob/main/docs/ctr2-usb-relay-design.md)),
so the knob is an independent SmartSDR API client. It binds to the AetherSDR
(or SmartSDR) station and drives that station's slices. No Wi-Fi setup is needed.

## Controls

| Input | Action |
| --- | --- |
| Turn knob | Tune the active slice by its step |
| Press knob | Next tuning step (10 Hz → 10 kHz); in parameter mode, back to tuning |
| Tap a tile | Knob adjusts that parameter (yellow outline); tap again for tuning |
| Long-press NR / NB / ANF | Toggle it on or off |
| Long-press AGC or AF | Cycle AGC mode (off/slow/med/fast) |
| Long-press FILT | Next tuning step |
| Tap a slice's **TX** label | Make that slice the transmit slice (grey = not TX, red outline = TX, filled = on air) |
| Swipe down | Open the menu (below) |

### Menu

Swipe down anywhere on the face to open it. It shows a scrolling list of
values for the active slice and the transmitter:

- Tap a row to put it on the knob (yellow outline); tap it again to release it.
- With no row selected, the knob scrolls the list.
- Tap an on/off row (Mute, Diversity, Tune lock, Break-in) to flip it.
- Long-press a row that has a level and a switch (RIT, XIT, Squelch, WNB, APF,
  Compander, Monitor) to flip its switch.
- For list rows (Mode, antennas, Band, AGC mode, NR/ANF mode), the knob
  previews the choice in yellow. It is sent once the knob rests for 0.7 s.

To close it, tap the grip at the bottom, swipe up from the grip, press the
knob, or leave it for 8 s.

Rows:

- **Receive:** Mode, RX antenna, Band, RIT, Filter low/high, AGC mode,
  Squelch, NR mode (NR/NRL/NRS/RNN/NRF) and amount, ANF mode (ANF/ANFL/ANFT)
  and amount, WNB, APF (CW only), Mute, Pan, Diversity, DAX channel, Tune lock.
- **Transmit:** RF power, Tune power, Mic gain, Compander, Monitor, TX antenna,
  XIT, and in CW also CW speed, CW pitch and Break-in.

Nothing in the menu keys the transmitter. VOX and TUNE are deliberately left
out. To add a row, add one entry to the table in `main/menu_items.c`.

Tiles: **AF** (audio level), **AGC** (threshold), **FILT** (width, 50 Hz per click,
about the passband centre), and **NR / NB / ANF** (level).

## Build and flash

Requires ESP-IDF 6.0 or later.

```sh
. ~/esp/esp-idf-v6.0.3/export.sh
idf.py build
```

The firmware takes over the native USB port as a HID device, so the board's
USB serial port is not available while it runs. To flash: hold **BOOT**, tap
**RESET**, release BOOT. The board then enumerates as the ROM's USB serial
port.

```sh
idf.py -p /dev/ttyACM0 flash
```

Tap RESET to run it. Logs go to UART0 on the 4-pin UART header (115200).

## Use with AetherSDR

1. Connect AetherSDR to the radio.
2. Open the **CTR2 Proxy** applet, set Mode to **USB**, pick **AetherKnob**
   (`AETHER_KNOB`), and press **Start**.
3. The knob's screen goes from "Waiting for AetherSDR" to the meter face once
   it has bound to your station.

On Linux the applet offers to install a udev rule the first time. The rule
needs AetherSDR with AetherKnob support (the `AETHER_KNOB` product string).

## Layout

| Path | What |
| --- | --- |
| `main/board.c` | Display (ST7701S over 3-wire SPI + RGB), touch, knob (PCNT), PCF8574 |
| `main/usb_link.c` | TinyUSB vendor HID, device side of CTR2 link v0, radio task |
| `main/ctr2_link.{c,h}` | Link framing, MIT, copied from AetherSDR `tools/ctr2-firmware-reference` |
| `main/flex_api.c` | SmartSDR API client: bind, slices, commands, VITA-49 meters |
| `main/ui.c` | LVGL 9 face |
| `main/menu_items.c` | Menu rows: keys, ranges, commands (host-tested) |
| `main/menu.c` | Swipe-down menu screen |
| `test/` | Host test of the API client (`make -C test check`) |
| `docs/smartsdr-api-notes.md` | Protocol facts the client relies on, from FlexLib |

## Status

What is tested:

- The firmware builds.
- The API client passes host tests against a scripted radio session.

Not yet verified on hardware:

- Display bring-up.
- Encoder counts per detent (`KNOB_COUNTS_PER_DETENT` in `board.c`).
- Touch orientation.
- Meters over the relay's UDP path. No CTR2 has exercised that path on hardware yet.

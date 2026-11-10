# SmartMTR-ESP32

<table><tr><td>

### Based on SmartMTR by Dariusz Socha, SQ5X

The design of this dial — the round face, the S-meter arcs, the side gauges,
the center dock, its layout and styling — comes from
**[SmartMTR](https://smartmtr.net/)**, created by **Dariusz Socha (SQ5X)**.

This port is published with his kind permission. All credit for the SmartMTR
design is his: **https://smartmtr.net/**

</td></tr></table>

A FlexRadio controller on the **Waveshare ESP32-S3-Knob-Touch-LCD-1.8**
(ESP32-S3R8, 360×360 round touch screen, and a rotary dial that turns in
detents but does not click).

The face is modeled on [SmartMTR](https://smartmtr.net/) by Dariusz Socha, SQ5X: S-meter arcs for two
slices, SWR/COMP/PWR/MIC needles, and the frequency readout, with a dock of touch
tiles in the middle.

It plugs into the PC running AetherSDR by USB. AetherSDR's **CTR2 Proxy**
applet relays it to the radio over the CTR2 USB link v0
([spec](https://github.com/aethersdr/AetherSDR/blob/main/docs/ctr2-usb-relay-design.md)),
so the knob is an independent SmartSDR API client. It binds to the AetherSDR
(or SmartSDR) station and drives that station's slices. No Wi-Fi setup is needed.

## Controls

There are two kinds of input, and this document keeps their names apart:

- **On the screen:** **tap** (a short touch), **touch-and-hold**, and **swipe**.
- **On the dial:** **turn** it, one **detent** at a time.

| Input | Action |
| --- | --- |
| Turn the dial | Tune the active slice by its step |
| Tap the face | Next tuning step (10 Hz → 10 kHz); in parameter mode, back to tuning |
| Touch-and-hold the face | Tune lock on/off for the active slice; the face shows **LOCKED** and turning does not tune |
| Tap a tile | Turning the dial adjusts that parameter (yellow outline); tap it again to tune |
| Touch-and-hold NR / NB / ANF | Toggle it on or off |
| Touch-and-hold AGC or AF | Step AGC mode (off/slow/med/fast) |
| Touch-and-hold FILT | Next tuning step |
| Tap a slice's **TX** label | Make that slice the transmit slice (gray = not TX, red outline = TX, filled = on air) |
| Swipe down | Open the menu (below) |

"The face" means its background in the inner part of the face: away from
the tiles, the spectrum and the TX labels, and not out on the meter ring.

### Menu

Swipe down anywhere on the face to open it. It is a scrolling list of values
for the active slice and the transmitter, and every row can be changed
without leaving it:

- **Levels** (RIT, filter edges, squelch, NR amount, RF power, …) have a fader.
- **Lists** (Mode, antennas, Band, NR/ANF mode) have a stepped fader.
- **On/off settings** have a switch. Rows with both a level and an on/off
  setting (RIT, XIT, Squelch, WNB, APF, Compander, Monitor) have both.
- **AGC** has a fader for the amount and a mode button (Off/Slow/Med/Fast).

By touch:

- **Swipe** the list up or down to scroll it. A vertical swipe that starts
  on a fader still scrolls the list.
- **Drag a fader sideways** to set the value; levels go to the radio as you
  drag, and a list choice is sent when you lift your finger.
- **Tap a switch** or the AGC mode button to flip it or step it.
- **Tap a row** to put it in edit for the dial (yellow outline); tap it again
  to release it.

With the dial, on a row in edit: a level steps, a list previews its next
choice in yellow and sends it once the dial rests for 0.7 s, and an on/off row
turns on (clockwise) or off (anticlockwise). With nothing in edit, turning
moves the highlight (white outline) down or up the list.

To close it, tap the grip at the bottom, swipe up from the grip, or leave it
for 8 s.

Rows:

- **Receive:** Mode, RX antenna, Band, RIT, Filter low/high, AGC (mode and amount),
  Squelch, NR mode (NR/NRL/NRS/RNN/NRF) and amount, ANF mode (ANF/ANFL/ANFT)
  and amount, WNB, APF (CW only), Mute, Pan, Diversity, DAX channel, Tune lock.
- **Transmit:** RF power, Tune power, Mic gain, Compander, Monitor, TX antenna,
  XIT, and in CW also CW speed, CW pitch and Break-in.

Nothing in the menu keys the transmitter. VOX and TUNE are deliberately left
out. To add a row, add one entry to the table in `main/menu_items.c`.

### S-meters

The bars and their white peak markers use AetherSDR's SmartMTR ballistics
(`main/meter_ballistics.c`, ported from `SmartMtrWidget`):

- **Bar:** jumps up in about 18 ms and sags back in about 270 ms.
- **Peak marker:** shows the highest level of the last 3 s. Once that peak
  leaves the window, the marker glides down at a steady rate, and it never
  sits below the bar.
- **Scale:** linear in dB from S0 to S9+40, so every dB is the same length
  and S9 sits a little past the middle.

### Center: spectrum or tiles

With an AetherSDR that sends it, the center shows a 32-bar spectrum (with peak
hold) of the receive audio you are hearing, like SmartMTR's: after the radio's
DSP, AetherSDR's noise reduction and its RX chain up to and including the EQ.
While transmitting it shows your transmit audio in red, across the TX filter
width. Otherwise it spans 0 Hz to the active slice's filter width, so a 600 Hz CW filter shows
0–600 Hz and a 6 kHz SSB filter 0–6 kHz (at most 12 kHz, the limit of the
24 kHz audio). Bar heights are in dB, and the bars are spaced
logarithmically in frequency like an audio analyzer, starting from a low edge
of about a sixtieth of the span. The scale relabels itself to match. **Tap** it to show the tiles below instead; they return to the
spectrum after 8 s unused, unless a tile is on the dial. A **sideways
swipe** switches either way. Without a spectrum the tiles stay.

The spectrum is a CTR2 link extension, negotiated through HID Feature
report `0x02` (see AetherSDR `docs/ctr2-usb-relay-design.md`,
"Extensions"). A CTR2 never offers or receives it.

Tiles: **AF** (audio level), **AGC** (threshold), **FILT** (width, 50 Hz per detent,
about the passband center), and **NR / NB / ANF** (level).

## Build and flash

Requires ESP-IDF 6.0 or later.

```sh
. ~/esp/esp-idf-v6.0.3/export.sh
idf.py build
```

### Flashing

The board has two chips on one USB-C port, and the way round the plug goes
picks which one the PC sees. One way gives the ESP32-S3 (native USB, the
`USB JTAG/serial debug unit` or, running this firmware, `AETHER_KNOB`); the
other gives the second chip, an ESP32 for Bluetooth audio, through a CH340
serial adapter. AetherKnob runs on the ESP32-S3 only; if the PC shows a
CH340, turn the plug over.

To flash: hold **BOOT**, unplug and replug the USB-C cable, release BOOT. The
screen stays dark and the ROM's USB serial port appears. Then:

```sh
idf.py -p /dev/ttyACM0 flash
```

Replug to run it. UART0 is not brought out on this board, so the firmware has
no log output in normal use.

### Debugging

`sdkconfig.bringup` builds a firmware that leaves the USB port as the chip's
serial console, with logs, and logs dial turns, raw dial edges and touches,
instead of starting the link to AetherSDR. It also flashes without the BOOT
routine:

```sh
idf.py -B build-bringup -D SDKCONFIG=build-bringup/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.bringup" build
```

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
| `main/board.{h,c}` | Display (ST77916 over QSPI), touch (CST816), dial turn (one pulse per detent, debounced) |
| `main/usb_link.c` | TinyUSB vendor HID, device side of CTR2 link v0, radio task |
| `main/ctr2_link.{c,h}` | Link framing, MIT, copied from AetherSDR `tools/ctr2-firmware-reference` |
| `main/flex_api.c` | SmartSDR API client: bind, slices, commands, VITA-49 meters |
| `main/ui.c` | LVGL 9 face |
| `main/menu_items.c` | Menu rows: keys, ranges, commands (host-tested) |
| `main/menu.c` | Swipe-down menu screen |
| `test/` | Host test of the API client (`make -C test check`) |
| `test/preview/` | Renders the screens on the PC (`make faces`) and drives the face and menu with a scripted finger and dial (`make touchtest`) |
| `docs/smartsdr-api-notes.md` | Protocol facts the client relies on, from FlexLib |

## Status

What is tested:

- The firmware builds.
- The API client passes host tests against a scripted radio session.

Verified on hardware: the display, touch, dial and the link through AetherSDR
to a FLEX-8600, with the S-meters.

## License

GPL-3.0; see [LICENSE](LICENSE). The two `ctr2_link` files are MIT. The design
credit to Dariusz Socha, SQ5X, in [NOTICE.md](NOTICE.md) must be kept in any copy or
derivative.

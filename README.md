# AetherKnob

> **Not for distribution.** The face and meter design are closely based on
> SmartMTR by Dariusz Socha. Keep this repository local until he has been
> asked about distribution. See [NOTICE.md](NOTICE.md).

A FlexRadio controller on the **Elecrow CrowPanel 2.1" HMI ESP32 Rotary
Display** (DHE03921D: ESP32-S3R8, 480×480 round IPS touch screen, rotary dial that also clicks).
The face is modelled on [SmartMTR](https://smartmtr.net/): S-meter arcs for two
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
- **On the dial:** **turn** it, one **detent** at a time, and **click** it (push it
  down until it clicks).

| Input | Action |
| --- | --- |
| Turn the dial | Tune the active slice by its step |
| Click the dial | Next tuning step (10 Hz → 10 kHz); in parameter mode, back to tuning |
| Click-and-hold the dial (0.6 s) | Tune lock on/off for the active slice; the face shows **LOCKED** and turning does not tune |
| Tap a tile | Turning the dial adjusts that parameter (yellow outline); tap it again to tune |
| Touch-and-hold NR / NB / ANF | Toggle it on or off |
| Touch-and-hold AGC or AF | Step AGC mode (off/slow/med/fast) |
| Touch-and-hold FILT | Next tuning step |
| Tap a slice's **TX** label | Make that slice the transmit slice (grey = not TX, red outline = TX, filled = on air) |
| Swipe down | Open the menu (below) |

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

With the dial:

- **Turn** to move the highlight (white outline) down or up the list; the
  list scrolls with it.
- **Click** to put the highlighted row in edit (yellow outline). Turning then
  changes it: a level steps, a list previews its next choice in yellow, and
  an on/off row turns on (clockwise) or off (anticlockwise).
- **Click while editing:** on a row with a switch it flips the switch, and on
  AGC it steps the mode; both stay in edit. On any other row it leaves edit,
  and sends a previewed choice at once. Otherwise a choice is sent when the
  dial rests for 0.7 s.
- **Click-and-hold** leaves edit and drops a previewed choice. With nothing
  in edit, it closes the menu.

To close it, tap the grip at the bottom, swipe up from the grip,
click-and-hold the dial (with nothing in edit), or leave it for 8 s.

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
- **Scale:** as in SmartMTR, S9 sits at the midpoint, with S0 to S9 on one
  side and S9 to +60 on the other.

### Centre: spectrum or tiles

With an AetherSDR that sends it, the centre shows a 32-bar spectrum (with peak
hold) of the receive audio you are hearing, like SmartMTR's: after the radio's
DSP, AetherSDR's noise reduction and its RX chain up to and including the EQ.
While transmitting it shows your transmit audio in red, across the TX filter
width. Otherwise it spans 0 Hz to the active slice's filter width, so a 600 Hz CW filter shows
0–600 Hz and a 6 kHz SSB filter 0–6 kHz (at most 12 kHz, the limit of the
24 kHz audio). Bar heights are in dB, and the bars are spaced
logarithmically in frequency like an audio analyser, starting from a low edge
of about a sixtieth of the span. The scale relabels itself to match. **Tap** it to show the tiles below instead; they return to the
spectrum after 8 s unused, unless a tile is on the dial. A **sideways
swipe** switches either way. Without a spectrum the tiles stay.

The spectrum is a CTR2 link extension, negotiated through HID Feature
report `0x02` (see AetherSDR `docs/ctr2-usb-relay-design.md`,
"Extensions"). A CTR2 never offers or receives it.

Tiles: **AF** (audio level), **AGC** (threshold), **FILT** (width, 50 Hz per detent,
about the passband centre), and **NR / NB / ANF** (level).

## Connections (back of the board)

| Connector | Pins | Use |
| --- | --- | --- |
| **USB-5V-IN** (4-pin MX1.25) | VCC, D−, D+, GND | Power and native USB to the PC, through the MX1.25-to-USB cable that ships with the board. This carries the HID link to AetherSDR and is also the flashing port. |
| **UART** (4-pin) | RX, TX, 5V, GND | Firmware logs at 115200. Use a 3.3 V USB-serial adapter, and leave its power pin off the 5V pin. |
| **I2C** (4-pin) | SCL, SDA, 5V, GND | Spare. It is the same bus as the touch controller (0x15) and the PCF8574 (0x21), so an added device needs another address. |
| **RESET**, **BOOT** | | Flashing, below. |

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

Tap RESET to run it. Within about 3 s the board re-enumerates as
`AetherSDR AETHER_KNOB`. If it is still `USB JTAG/serial debug unit`, the
reset after flashing left it in download mode: tap RESET again, or run
`python -m esptool --port <port> --after watchdog-reset chip-id`. Logs go to UART0 on the 4-pin UART header (115200).

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
| `main/board.c` | Display (ST7701S over 3-wire SPI + RGB), touch, dial turn (PCNT) and click (PCF8574) |
| `main/usb_link.c` | TinyUSB vendor HID, device side of CTR2 link v0, radio task |
| `main/ctr2_link.{c,h}` | Link framing, MIT, copied from AetherSDR `tools/ctr2-firmware-reference` |
| `main/flex_api.c` | SmartSDR API client: bind, slices, commands, VITA-49 meters |
| `main/ui.c` | LVGL 9 face |
| `main/menu_items.c` | Menu rows: keys, ranges, commands (host-tested) |
| `main/menu.c` | Swipe-down menu screen |
| `test/` | Host test of the API client (`make -C test check`) |
| `test/preview/` | Renders the screens on the PC (`make faces`) and drives the menu with a scripted finger and dial (`make touchtest`) |
| `docs/smartsdr-api-notes.md` | Protocol facts the client relies on, from FlexLib |

## Status

What is tested:

- The firmware builds.
- The API client passes host tests against a scripted radio session.

Not yet verified on hardware:

- Display bring-up.
- Encoder counts per detent (`DIAL_COUNTS_PER_DETENT` in `board.c`).
- Touch orientation.
- Meters over the relay's UDP path. No CTR2 has exercised that path on hardware yet.

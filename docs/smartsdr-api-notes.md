# SmartSDR API notes for AetherKnob

Wire facts this firmware relies on, taken from FlexLib v4.1.5 (the protocol
authority) and cross-checked against AetherSDR's client.

## Lines (TCP, `\n`-terminated, dispatch on first char)
- `V<a.b.c.d>` version, `H<hex>` our client handle.
- Command `C<seq>|<cmd>`; reply `R<seq>|<hex status>|<msg>` (0 = OK).
- Status `S<handle>|<object> <key=val ...>`; message `M<hex>|<text>`.

## Session (non-GUI client)
1. `client program AetherKnob` — never `client gui`.
2. `sub client all` → `client 0x<h> connected client_id=<uuid> program=<p> station=<s>`
   (station uses 0x7F for spaces). Pick the GUI client, send
   `client bind client_id=<uuid>`. Its slices are those whose `client_handle`
   equals that client's handle.
3. `sub slice all`, `sub tx all`, `sub meter all`.
4. `keepalive enable`, then `ping` every 1 s (radio drops a silent client).

## Slice control
- `slice tune <N> <MHz %.6f>`; `filt <N> <lo> <hi>` (Hz, lo < hi).
- `slice set <N> key=value`: mode, audio_level, agc_mode, agc_threshold,
  nr/nr_level, nb/nb_level, anf/anf_level, rxant, step, active.
- Status keys: RF_frequency, mode, filter_lo, filter_hi, active, client_handle,
  audio_level, agc_mode, agc_threshold, nr, nb, anf (+_level), rxant, step,
  step_list, tx, index_letter, in_use (0 = removed).

## Meters
- Definitions: `meter <i>.src=SLC#<i>.num=0#<i>.nam=LEVEL#<i>.unit=dBm#...`;
  removal `meter <i> removed`.
- Names: LEVEL (src SLC, num = slice), FWDPWR, REFPWR, SWR, MICPEAK, COMPPEAK.
- int16 scale by unit: dB/dBm/dBFS/SWR ÷128; Volts/Amps ÷256; degC/degF ÷64;
  Watts/Percent raw.

## UDP over the CTR2 USB link
- The radio cannot see our UDP endpoint (it is the relay's socket), which is
  FlexLib's WAN case: send the ASCII DATAGRAM `client udp_register
  handle=0x<HANDLE>` (uppercase hex, no padding) to radio port 4992, repeated
  until any Flex-OUI VITA packet arrives (FlexLib Radio.cs RegisterUDP).
  Never `client udpport`: the port would be on the PC. See AetherSDR
  docs/ctr2-usb-relay-design.md.
- VITA-49, big-endian. Meters: ExtDataWithStream, OUI 0x001C2D,
  packet class 0x8002. Payload = (uint16 id, int16 value) pairs; last wins.
- Link budget ~7 KB/s; the host drops datagrams beyond ~3 KB queued.

## Menu values: command key vs status key

FlexLib v4.1.5; the radio reports some values under a different key than the
one that sets them.

| Set with | Reported as |
| --- | --- |
| `slice set N lms_nr=` / `speex_nr=` / `rnnoise=` / `lms_anf=` | `nrl` / `nrs` / `rnn` / `anfl` |
| `transmit set miclevel=` | `mic_level` |
| `transmit set mon=` | `sb_monitor` |
| `transmit set am_carrier=` | `am_carrier_level` |
| `cw wpm N` | `speed` (on `transmit`) |
| `cw pitch N` | `pitch` (on `transmit`, radio-wide) |

- Filter edges: `filt N lo hi`; FM/NFM refuse changes; USB/DIGU/FDV lo ≥ 0;
  LSB/DIGL hi ≤ 0; CW ±(12000 − pitch); width ≥ 50 Hz.
- Band: `display pan set 0x<pan> band=<id>` (pan from the slice's `pan=`);
  ids 160…6, 33 = WWV, 34 = GEN. The radio recalls the band stack and may
  re-create the slice.
- Tune lock: `slice lock N` / `slice unlock N`, status `lock`.
- `transmit band …` status lines are per-band settings, not current ones.
- NRL/NRS/RNN/NRF/ANFL/ANFT are FLEX-8000/Aurora features; a 6000 ignores them.

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
- Register by sending a DATAGRAM (one 0x00 byte) to radio port 4992; never
  `client udpport` (the port would be on the PC). See AetherSDR
  docs/ctr2-usb-relay-design.md.
- VITA-49, big-endian. Meters: ExtDataWithStream, OUI 0x001C2D,
  packet class 0x8002. Payload = (uint16 id, int16 value) pairs; last wins.
- Link budget ~7 KB/s; the host drops datagrams beyond ~3 KB queued.

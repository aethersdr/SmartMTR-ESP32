# Credits

## SmartMTR design

The user-interface design of this dial is based on **SmartMTR** by
**Dariusz Socha, SQ5X**: https://smartmtr.net/

That covers the round face, its S-meter arcs, side gauges, centre dock, and
overall layout and styling (`main/ui.c` and the menu's visual styling in
`main/menu.c`). It is used with his permission, on the condition that he is
credited and his website is linked. Keep this credit, and the one in the
README, in any copy or derivative of this project.

## Licences

- This project is licensed under the GNU General Public License v3.0; see
  [LICENSE](LICENSE).
- `main/ctr2_link.c` and `main/ctr2_link.h` are MIT-licensed, copied from
  AetherSDR's CTR2 firmware reference (`tools/ctr2-firmware-reference`).
- The meter ballistics (`main/meter_ballistics.c`) are ported from
  AetherSDR's `SmartMtrWidget`.

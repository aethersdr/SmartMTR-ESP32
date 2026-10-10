# Not for distribution: the dial's UI design

The **user-interface design** of this ESP32 dial is closely based on
**SmartMTR** by **Dariusz Socha** (https://smartmtr.net/): the round face, its
S-meter arcs, side gauges, centre dock and overall layout and styling. That is
`main/ui.c`, the menu's visual styling in `main/menu.c`, and every render
under `test/preview/`.

That UI design must not be published, shared or distributed until Dariusz
Socha has been asked about distribution and has agreed.

Everything else here is ours and not restricted by this notice:

- the USB link and its extensions;
- the SmartSDR API client and the menu's settings logic;
- the meter ballistics, ported from AetherSDR's own code;
- the board support;
- the tests.

Because the UI ships in this same repository, the repository as a whole stays
local, and a pre-push hook in this clone refuses every push. To publish the
rest, split the UI out first.

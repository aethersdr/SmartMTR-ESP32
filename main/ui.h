// SmartMTR-style round face: S-meter arcs for two slices, TX needles, the
// frequency readout and a dock of touch tiles that pick what the knob turns.
#pragma once

#include "lvgl.h"

// Builds the screen and starts its refresh timer. Call once, after
// board_init(), with the LVGL port lock held.
void ui_start(lv_display_t *disp);

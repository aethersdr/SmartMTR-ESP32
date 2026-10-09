// SmartMTR-style round face: S-meter arcs for two slices, TX needles, the
// frequency readout and a dock of touch tiles that pick what turning the dial changes.
#pragma once

#include "lvgl.h"

// Builds the screen and starts its refresh timer. Call once, after
// board_init(), with the LVGL port lock held.
void ui_start(lv_display_t *disp);

// Sets label text only when it differs, so unchanged labels are not redrawn.
void ui_set_text(lv_obj_t *label, const char *text);
void ui_set_textf(lv_obj_t *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

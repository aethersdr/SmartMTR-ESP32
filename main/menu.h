// Swipe-down menu: a scrolling list of every row in menu_items.c for the
// focus slice and the transmitter.
//
// Touch: tap a row to put it on the dial (tap again to release), tap a
// toggle to flip it, touch-and-hold a row with a switch or mode to flip it.
// Dial: turn to move the highlight; click to act on the row (flip a toggle,
// or put the row in edit, where turning changes it and a click flips its
// switch, steps its mode, or confirms the choice); click-and-hold to back
// out of edit, or to close the menu.
// Also closes on a tap or swipe up on the grip, or 8 s idle.
#pragma once

#include "lvgl.h"
#include "model.h"

#include <stdbool.h>
#include <stdint.h>

void menu_create(lv_obj_t *screen);
void menu_open(void);
void menu_close(void);
bool menu_is_open(void);

// Called by the UI refresh: the model snapshot and the focus slice (-1: none).
void menu_update(const knob_model_t *m, int focus, bool model_changed);

// Dial input while the menu is open.
void menu_dial_turn(int detents);
void menu_dial_click(void);   // short click, sent on release
void menu_dial_hold(void);    // click-and-hold, sent once the hold time passes

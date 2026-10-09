// Swipe-down menu: a scrolling list of every row in menu_items.c for the
// focus slice and the transmitter. Tap a row to put it on the knob, tap a
// toggle to flip it, long-press a level row with a switch to flip the switch.
// Closes on a swipe up from the grip, a knob press, or 8 s without use.
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

// Knob input while the menu is open.
void menu_knob_turn(int detents);
void menu_knob_press(void);

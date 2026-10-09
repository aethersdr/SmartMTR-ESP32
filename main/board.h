// Elecrow CrowPanel 2.1" rotary display (DHE03921D): ESP32-S3R8, ST7701S
// 480x480 RGB panel, CST8xx touch, a rotary dial that also clicks, PCF8574
// expander for display power/reset and the dial click.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

// Powers the panel, starts LVGL and registers the touch input device.
// Returns the LVGL display; call lvgl_port_lock() before touching LVGL.
lv_display_t *board_init(void);

// Two physical inputs, distinct from touches on the screen:
//  - turning the dial, counted in detents (positive = clockwise);
//  - clicking the dial, i.e. pushing it down until it clicks.
int board_dial_take_detents(void);
// Whether the dial is clicked down now; the caller's ~33 ms poll debounces it.
bool board_dial_is_down(void);

void board_backlight(uint8_t percent);

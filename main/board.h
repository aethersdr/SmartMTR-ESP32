// Elecrow CrowPanel 2.1" rotary display (DHE03921D): ESP32-S3R8, ST7701S
// 480x480 RGB panel, CST8xx touch, quadrature knob with push, PCF8574
// expander for display power/reset and the knob button.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

// Powers the panel, starts LVGL and registers the touch input device.
// Returns the LVGL display; call lvgl_port_lock() before touching LVGL.
lv_display_t *board_init(void);

// Encoder detents since the last call (positive = clockwise).
int board_knob_take_detents(void);
// Knob push state, debounced by the caller's poll rate (~20 ms).
bool board_knob_pressed(void);

void board_backlight(uint8_t percent);

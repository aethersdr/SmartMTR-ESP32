// The Waveshare ESP32-S3-Knob-Touch-LCD-1.8: ESP32-S3R8, ST77916 360x360
// round QSPI panel, CST816 touch, and a dial that turns in detents but does
// not click. The board's second chip, an ESP32 for Bluetooth audio, is not
// used.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define BOARD_LCD_SIZE 360

// Powers the panel, starts LVGL and registers the touch input device.
// Returns the LVGL display; call lvgl_port_lock() before touching LVGL.
lv_display_t *board_init(void);

// Turns of the dial since the last call, in detents (positive = clockwise).
int board_dial_take_detents(void);

void board_backlight(uint8_t percent);

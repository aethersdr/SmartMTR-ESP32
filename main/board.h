// The board AetherKnob runs on, chosen at build time (menuconfig: AetherKnob
// board). Every board has a round touch display, a dial that turns in detents
// and native USB to the PC:
//  - Elecrow CrowPanel 2.1" rotary display (DHE03921D): ESP32-S3R8, ST7701S
//    480x480 RGB panel, CST8xx touch, a dial that also clicks, PCF8574
//    expander for display power/reset and the dial click.
//  - Waveshare ESP32-S3-Knob-Touch-LCD-1.8: ESP32-S3R8, ST77916 360x360 QSPI
//    panel, CST816 touch, a dial that only turns (no click).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef KNOB_HOST_TEST
#include "sdkconfig.h"
#endif
#include "lvgl.h"

#if defined(CONFIG_AETHERKNOB_BOARD_WAVESHARE_KNOB18)
#define BOARD_LCD_SIZE        360
#define BOARD_HAS_DIAL_CLICK  0
#else
#define BOARD_LCD_SIZE        480
#define BOARD_HAS_DIAL_CLICK  1
#endif

// Powers the panel, starts LVGL and registers the touch input device.
// Returns the LVGL display; call lvgl_port_lock() before touching LVGL.
lv_display_t *board_init(void);

// Two physical inputs, distinct from touches on the screen:
//  - turning the dial, counted in detents (positive = clockwise);
//  - clicking the dial, i.e. pushing it down until it clicks.
int board_dial_take_detents(void);
// Whether the dial is clicked down now; the caller's ~33 ms poll debounces it.
// Always false on a board without a dial click (BOARD_HAS_DIAL_CLICK 0),
// where a tap on the face stands in for it (ui.c).
bool board_dial_is_down(void);

void board_backlight(uint8_t percent);

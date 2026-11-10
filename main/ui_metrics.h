// Sizes for the face and the menu. The layout is drawn for the CrowPanel's
// 480x480 display; PX() scales a length from that to this board's display,
// and the fonts step down with it.
#pragma once

#include <math.h>

#include "board.h"
#include "lvgl.h"

#define UI_SIZE BOARD_LCD_SIZE
#define UI_HALF (UI_SIZE / 2)

// A length on the 480 px layout, in this board's pixels.
#define PX(v) ((int32_t)lroundf((float)(v) * (float)UI_SIZE / 480.0f))

// Fonts by their size on the 480 px layout.
#if UI_SIZE == 480
#define UI_FONT_14 (&lv_font_montserrat_14)
#define UI_FONT_20 (&lv_font_montserrat_20)
#define UI_FONT_28 (&lv_font_montserrat_28)
#define UI_FONT_40 (&lv_font_montserrat_40)
#else
#define UI_FONT_14 (&lv_font_montserrat_12)
#define UI_FONT_20 (&lv_font_montserrat_16)
#define UI_FONT_28 (&lv_font_montserrat_22)
#define UI_FONT_40 (&lv_font_montserrat_30)
#endif

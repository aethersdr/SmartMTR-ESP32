// The round face. Its design is based on SmartMTR by Dariusz Socha, SQ5X
// (https://smartmtr.net/), used with his permission; see NOTICE.md.
#include "ui.h"

#include "board.h"
#include "menu.h"
#include "menu_items.h"
#include "meter_ballistics.h"
#include "model.h"
#include "usb_link.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CX 240
#define CY 240

// SmartMTR palette.
#define COL_FACE      lv_color_hex(0x07131D)
#define COL_TRACK     lv_color_hex(0x13557A)
#define COL_LEVEL     lv_color_hex(0xFF2D55)
#define COL_SCALE     lv_color_hex(0x37C8F5)
#define COL_OVER      lv_color_hex(0xFF2D55)
#define COL_TEXT      lv_color_hex(0xE8EEF2)
#define COL_DIM       lv_color_hex(0x6F8796)
#define COL_TILE      lv_color_hex(0x0E2A3B)
#define COL_TILE_ON   lv_color_hex(0x1C6E99)
#define COL_SELECT    lv_color_hex(0xFFD60A)

// S-meter geometry: S0..S9 fill half the arc, S9..S9+40 the rest.
#define ARC_RADIUS    222
#define ARC_WIDTH     20
#define ARC_SPAN      116          // degrees
#define TOP_START     (270 - ARC_SPAN / 2)
#define BOTTOM_START  (90 + ARC_SPAN / 2)   // runs anticlockwise, S1 on the left

typedef enum { MODE_TUNE, MODE_PARAM } knob_mode_t;

typedef struct {
    lv_obj_t *arc;
    lv_obj_t *peak;
    lv_obj_t *freq;
    lv_obj_t *mode;
    lv_obj_t *filter;
    lv_obj_t *tx;
    bool top;
    int slice;          // slice shown, -1 for none (the TX badge's target)
    int meter_slice;    // slice the ballistics below belong to
    meter_smoother_t bar;       // SmartMTR bar ballistics
    meter_extremes_t extremes;  // SmartMTR peak marker
    int tx_state;       // last drawn TX badge state
    int32_t peak_angle;
} slice_view_t;

typedef struct {
    lv_obj_t *needle;
    lv_point_precise_t pts[2];
    int32_t tip_x, tip_y;
    int cx, cy, len;
    float lo, hi;
} gauge_t;

typedef struct {
    lv_obj_t *btn;
    lv_obj_t *label;
    knob_param_t param;
    knob_action_kind_t toggle;   // touch-and-hold action
    const char *name;
    bool on, selected;           // last drawn state
} tile_t;

static slice_view_t s_top, s_bottom;
static gauge_t s_swr, s_comp, s_pwr, s_mic;
static tile_t s_tiles[6];
static lv_obj_t *s_status_panel, *s_status_label, *s_step_label, *s_station_label;
static knob_mode_t s_mode = MODE_TUNE;
static knob_param_t s_param = PARAM_AF;
static uint32_t s_seen_revision = UINT32_MAX;
// A dial click is reported on release; holding it down this long is a
// click-and-hold instead, reported once when the time passes.
#define DIAL_HOLD_MS 600

static bool s_btn_was_down;
static bool s_hold_fired;
static uint32_t s_dial_down_ms;
static bool s_locked_drawn;
static knob_model_t s_snap;
static uint32_t s_tiles_used_ms;     // last tile touch, for the dock's return to the spectrum
static uint32_t s_last_frame_ms;     // for the meter ballistics' elapsed time

// ---------------------------------------------------------------- geometry

// Fraction 0..1 along the S-meter for a level in dBm.
// Fraction 0..1 along the S-meter for a level in dBm, as AetherSDR's SmartMTR
// maps it (SmartMtrConfig mapSignal): S0..S9 fill the lower half and S9..S9+40
// the upper half, each linear in dBm, so S9 sits at the midpoint. The dial tops
// out at +40, not SmartMTR's +60, so the over-S9 half has more room per dB.
static float smeter_fraction(float dbm)
{
    const float s0 = -127.0f, s9 = -73.0f, top = -33.0f;  // S9+40
    if (dbm <= s0)
        return 0.0f;
    if (dbm <= s9)
        return 0.5f * (dbm - s0) / (s9 - s0);
    if (dbm >= top)
        return 1.0f;
    return 0.5f + 0.5f * (dbm - s9) / (top - s9);
}

static void polar(int cx, int cy, float r, float deg, int32_t *x, int32_t *y)
{
    float a = deg * (float)M_PI / 180.0f;
    *x = cx + (int32_t)lroundf(r * cosf(a));
    *y = cy + (int32_t)lroundf(r * sinf(a));
}

// Screen angle of a meter fraction on the top (clockwise) or bottom arc.
static float meter_angle(bool top, float f)
{
    return top ? TOP_START + f * ARC_SPAN : BOTTOM_START - f * ARC_SPAN;
}

// ---------------------------------------------------------------- text

// LVGL redraws a label on every set_text, even to the same string, and the
// model changes with every meter packet; so only touch labels that changed.
void ui_set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0)
        lv_label_set_text(l, text);
}

void ui_set_textf(lv_obj_t *l, const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ui_set_text(l, buf);
}

// ---------------------------------------------------------------- builders

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *meter_arc(lv_obj_t *parent, bool top, int width, bool show_track, lv_color_t level)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, ARC_RADIUS * 2, ARC_RADIUS * 2);
    lv_obj_center(arc);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    if (top) {
        lv_arc_set_bg_angles(arc, TOP_START, TOP_START + ARC_SPAN);
    } else {
        lv_arc_set_bg_angles(arc, BOTTOM_START - ARC_SPAN, BOTTOM_START);
        lv_arc_set_mode(arc, LV_ARC_MODE_REVERSE);
    }
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, level, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, show_track ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    return arc;
}

// Ticks and labels: S1..S9 odd units, then +20/+40.
static void meter_scale(lv_obj_t *parent, bool top)
{
    static const struct { float dbm; const char *text; } marks[] = {
        {-121, "1"}, {-109, "3"}, {-97, "5"}, {-85, "7"}, {-73, "9"},
        {-53, "+20"}, {-33, "+40"},
    };
    static lv_point_precise_t tick_pts[2][16][2];
    // A tick per S unit to S9 (long on odd units), then every 10 dB to +40
    // (long on the labeled 20s).
    for (int i = 1; i < 14; ++i) {
        float dbm = i <= 9 ? -127.0f + 6.0f * (float)i : -73.0f + 10.0f * (float)(i - 9);
        bool over = dbm > -73.0f;
        bool major = over ? (i - 9) % 2 == 0 : i % 2 == 1;
        float a = meter_angle(top, smeter_fraction(dbm));
        int32_t x0, y0, x1, y1;
        polar(CX, CY, ARC_RADIUS - ARC_WIDTH - 6, a, &x0, &y0);
        polar(CX, CY, ARC_RADIUS - ARC_WIDTH - (major ? 20 : 12), a, &x1, &y1);
        lv_point_precise_t *p = tick_pts[top ? 0 : 1][i];
        p[0] = (lv_point_precise_t){x0, y0};
        p[1] = (lv_point_precise_t){x1, y1};
        lv_obj_t *t = lv_line_create(parent);
        lv_line_set_points(t, p, 2);
        lv_obj_set_style_line_width(t, 3, 0);
        lv_obj_set_style_line_color(t, over ? COL_OVER : COL_SCALE, 0);
    }
    for (size_t i = 0; i < sizeof marks / sizeof marks[0]; ++i) {
        float a = meter_angle(top, smeter_fraction(marks[i].dbm));
        int32_t x, y;
        polar(CX, CY, ARC_RADIUS - ARC_WIDTH - 36, a, &x, &y);
        bool nine = strcmp(marks[i].text, "9") == 0;
        lv_obj_t *l = label(parent, nine ? &lv_font_montserrat_28 : &lv_font_montserrat_20,
                            marks[i].dbm > -73.0f ? COL_OVER : COL_SCALE, marks[i].text);
        lv_obj_align(l, LV_ALIGN_CENTER, x - CX, y - CY);
    }
}

static void tx_badge_event(lv_event_t *e)
{
    slice_view_t *v = lv_event_get_user_data(e);
    if (v->slice >= 0) {
        knob_action_t a = {.kind = ACT_SET_TX, .arg = v->slice};
        usb_link_post_action(&a);
    }
}

static void slice_view(lv_obj_t *parent, slice_view_t *v, bool top)
{
    v->top = top;
    v->arc = meter_arc(parent, top, ARC_WIDTH, true, COL_LEVEL);
    // Peak-hold: a thin white sliver drawn by a second, track-less arc.
    v->peak = meter_arc(parent, top, ARC_WIDTH + 6, false, lv_color_white());
    lv_obj_set_style_arc_rounded(v->peak, false, LV_PART_INDICATOR);
    meter_scale(parent, top);

    // Offsets from the center. The top block reads mode, frequency, filter
    // downwards; the bottom block mirrors it.
    v->freq = label(parent, top ? &lv_font_montserrat_40 : &lv_font_montserrat_28, COL_TEXT, "--.---.---");
    lv_obj_align(v->freq, LV_ALIGN_CENTER, 0, top ? -92 : 110);
    v->mode = label(parent, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_align(v->mode, LV_ALIGN_CENTER, 0, top ? -134 : 142);
    // TX badge: filled while transmitting, outlined on the TX slice, gray on
    // the other; tapping the gray one moves TX to that slice.
    v->tx = label(parent, &lv_font_montserrat_14, COL_DIM, "TX");
    lv_obj_set_style_border_width(v->tx, 1, 0);
    lv_obj_set_style_border_color(v->tx, COL_DIM, 0);
    lv_obj_set_style_bg_color(v->tx, COL_LEVEL, 0);
    lv_obj_set_style_pad_hor(v->tx, 4, 0);
    lv_obj_set_clickable(v->tx, true);
    lv_obj_set_ext_click_area(v->tx, 14);
    lv_obj_add_event_cb(v->tx, tx_badge_event, LV_EVENT_SHORT_CLICKED, v);
    lv_obj_set_hidden(v->tx, true);
    v->slice = -1;
    v->meter_slice = -2;    // forces a fresh start on the first frame
    v->tx_state = -1;
    v->peak_angle = INT32_MIN;
    v->filter = label(parent, &lv_font_montserrat_20, COL_DIM, "");
    lv_obj_align(v->filter, LV_ALIGN_CENTER, 0, top ? -54 : 82);
}

static void gauge(lv_obj_t *parent, gauge_t *g, int cx, int cy, float lo, float hi,
                  const char *name, const char *lo_text, const char *hi_text)
{
    g->cx = cx;
    g->cy = cy;
    g->len = 22;
    g->lo = lo;
    g->hi = hi;
    lv_obj_t *dial = lv_obj_create(parent);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 58, 58);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    lv_obj_align(dial, LV_ALIGN_TOP_LEFT, cx - 29, cy - 29);
    lv_obj_set_clickable(dial, false);

    g->needle = lv_line_create(parent);
    lv_obj_set_style_line_width(g->needle, 3, 0);
    lv_obj_set_style_line_color(g->needle, COL_LEVEL, 0);
    lv_obj_set_style_line_rounded(g->needle, true, 0);

    // The name sits inside the dial, below the hub where the needle never goes.
    lv_obj_t *n = label(parent, &lv_font_montserrat_14, COL_TEXT, name);
    lv_obj_align(n, LV_ALIGN_CENTER, cx - CX, cy - CY + 17);
    lv_obj_t *l = label(parent, &lv_font_montserrat_14, COL_DIM, lo_text);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, cx - 44, cy + 6);
    lv_obj_t *h = label(parent, &lv_font_montserrat_14, COL_DIM, hi_text);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, cx + 32, cy + 6);
}

// Needle sweeps 135..405 degrees (SW to SE through the top).
static void gauge_set(gauge_t *g, float v)
{
    float f = (v - g->lo) / (g->hi - g->lo);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    int32_t x, y;
    polar(g->cx, g->cy, (float)g->len, 135.0f + f * 270.0f, &x, &y);
    if (x == g->tip_x && y == g->tip_y)
        return;
    g->tip_x = x;
    g->tip_y = y;
    g->pts[0] = (lv_point_precise_t){g->cx, g->cy};
    g->pts[1] = (lv_point_precise_t){x, y};
    lv_line_set_points(g->needle, g->pts, 2);
}

// ---------------------------------------------------------------- tiles

static void select_param(knob_param_t p)
{
    if (s_mode == MODE_PARAM && s_param == p) {
        s_mode = MODE_TUNE;               // tap the selected tile again: dial tunes again
    } else {
        s_mode = MODE_PARAM;
        s_param = p;
    }
    s_seen_revision = UINT32_MAX;         // force a redraw of the highlight
}

static void tile_event(lv_event_t *e)
{
    tile_t *t = lv_event_get_user_data(e);
    s_tiles_used_ms = lv_tick_get();
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
        select_param(t->param);
    } else if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        knob_action_t a = {.kind = t->toggle};
        usb_link_post_action(&a);
    }
}

static void tiles(lv_obj_t *parent)
{
    static const struct { const char *name; knob_param_t p; knob_action_kind_t toggle; } defs[] = {
        {"AF", PARAM_AF, ACT_AGC_NEXT},       // AF has no toggle; touch-and-hold steps AGC mode
        {"AGC", PARAM_AGC_T, ACT_AGC_NEXT},
        {"FILT", PARAM_FILTER_WIDTH, ACT_STEP_NEXT},
        {"NR", PARAM_NR_LEVEL, ACT_TOGGLE_NR},
        {"NB", PARAM_NB_LEVEL, ACT_TOGGLE_NB},
        {"ANF", PARAM_ANF_LEVEL, ACT_TOGGLE_ANF},
    };
    for (int i = 0; i < 6; ++i) {
        tile_t *t = &s_tiles[i];
        t->name = defs[i].name;
        t->param = defs[i].p;
        t->toggle = defs[i].toggle;
        t->btn = lv_button_create(parent);
        lv_obj_set_size(t->btn, 66, 46);
        lv_obj_align(t->btn, LV_ALIGN_CENTER, (i % 3 - 1) * 72, i < 3 ? -24 : 24);
        lv_obj_set_style_bg_color(t->btn, COL_TILE, 0);
        lv_obj_set_style_radius(t->btn, 10, 0);
        lv_obj_set_style_shadow_width(t->btn, 0, 0);
        lv_obj_set_style_border_color(t->btn, COL_SELECT, 0);
        lv_obj_set_style_border_width(t->btn, 0, 0);
        lv_obj_add_event_cb(t->btn, tile_event, LV_EVENT_ALL, t);
        t->label = label(t->btn, &lv_font_montserrat_14, COL_TEXT, t->name);
        lv_obj_set_style_text_align(t->label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(t->label);
    }
}

// ---------------------------------------------------------------- spectrum

// The center dock shows either the audio spectrum from AetherSDR or the
// tiles. A tap on the spectrum shows the tiles; they go back to the
// spectrum after DOCK_TILES_MS unused (unless a tile is on the dial). A
// sideways swipe switches either way. Without a spectrum the tiles stay.
#define DOCK_W          212
#define DOCK_H          96
#define DOCK_Y          8       // center offset of the dock
#define DOCK_TILES_MS   8000
#define SPECTRUM_STALE_MS 5000   // no frame for this long: fall back to the tiles
#define SPECTRUM_SETTLE_MS 1500  // frames for this long before switching to it
#define PEAK_DECAY      3       // level units per frame

typedef enum { DOCK_SPECTRUM, DOCK_TILES } dock_view_t;

static lv_obj_t *s_tiles_box, *s_spectrum;
static dock_view_t s_dock = DOCK_TILES;
static uint8_t s_bars[64], s_peaks[64];
static int s_bar_count;
static bool s_spectrum_tx;            // bars drawn in the TX color (transmit audio)
static int s_span_hz, s_low_hz;       // what the axis is labeled for
#define AXIS_LABELS 7
static lv_obj_t *s_axis[AXIS_LABELS];
static lv_obj_t *s_axis_unit;
static uint32_t s_spectrum_seen_ms;   // lv_tick when the last frame arrived
static uint32_t s_spectrum_stamp;     // model stamp of that frame
static uint32_t s_live_since_ms;      // start of the current unbroken run of frames

static bool spectrum_live(void)
{
    return s_bar_count > 0 && lv_tick_get() - s_spectrum_seen_ms < SPECTRUM_STALE_MS;
}

// Live long enough to switch to without flapping on a brief gap.
static bool spectrum_settled(void)
{
    return spectrum_live() && lv_tick_get() - s_live_since_ms >= SPECTRUM_SETTLE_MS;
}

static void show_dock(dock_view_t view)
{
    if (view == DOCK_SPECTRUM && !spectrum_live())
        view = DOCK_TILES;
    s_dock = view;
    lv_obj_set_hidden(s_spectrum, view != DOCK_SPECTRUM);
    lv_obj_set_hidden(s_tiles_box, view != DOCK_TILES);
    s_tiles_used_ms = lv_tick_get();
}

static void spectrum_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    // The same geometry label_axis() uses: bars fill exactly DOCK_W - 2 * pad
    // pixels from a.x1 + pad, so bar i sits over the frequencies its ticks
    // name. (LVGL areas are inclusive, and an integer slot width would shrink
    // the bars into the left of the axis.)
    const int pad = 10, axis = 18;
    const int x0 = a.x1 + pad, w = DOCK_W - 2 * pad;
    const int y_top = a.y1 + pad, y_base = a.y2 - axis;
    const int h = y_base - y_top;
    if (s_bar_count <= 0)
        return;
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_color = s_spectrum_tx ? COL_LEVEL : COL_SCALE;
    bar.bg_opa = LV_OPA_COVER;
    lv_draw_rect_dsc_t peak = bar;
    peak.bg_color = COL_TEXT;
    for (int i = 0; i < s_bar_count; ++i) {
        const int bx = x0 + i * w / s_bar_count;            // this bar's left edge
        const int bx_next = x0 + (i + 1) * w / s_bar_count; // the next bar's
        const int level = s_bars[i] * h / 255;
        lv_area_t r = {bx + 1, y_base - level, bx_next - 2, y_base};
        if (level > 0)
            lv_draw_rect(layer, &bar, &r);
        const int py = y_base - s_peaks[i] * h / 255;
        lv_area_t p = {bx + 1, py - 2, bx_next - 2, py};
        if (s_peaks[i] > s_bars[i])
            lv_draw_rect(layer, &peak, &p);
    }
}

// Fraction of the bar area across at frequency hz: log from low to span,
// or linear from 0 when low is 0.
static float axis_fraction(int low, int span, int hz)
{
    if (low > 0 && span > low)
        return logf((float)hz / (float)low) / logf((float)span / (float)low);
    return span > 0 ? (float)hz / (float)span : 0.0f;
}

// Ticks on a 1-2-5 sequence (log axis) or a clean step (linear), each centered
// where its frequency falls on the bars; "Hz" goes in the top-right corner.
static void label_axis(int low, int span)
{
    const int pad = 10, w = DOCK_W - 2 * pad;
    int ticks[AXIS_LABELS], count = 0;
    if (low > 0) {
        // Decades first (10, 100, 1k, 10k), then 2s and 5s wherever they keep
        // MIN_TICK_GAP clear of every tick already placed: the low end of a
        // log axis is crowded.
        enum { MIN_TICK_GAP = 30 };
        static const int decades[] = {10, 100, 1000, 10000};
        static const int fills[] = {20, 50, 200, 500, 2000, 5000, 20000};
        const int *passes[2] = {decades, fills};
        const size_t lens[2] = {sizeof decades / sizeof decades[0], sizeof fills / sizeof fills[0]};
        for (int p = 0; p < 2; ++p) {
            for (size_t i = 0; i < lens[p] && count < AXIS_LABELS; ++i) {
                const int hz = passes[p][i];
                if (hz < low || hz > span)
                    continue;
                const float x = axis_fraction(low, span, hz) * (float)w;
                bool clear = true;
                for (int j = 0; j < count; ++j)
                    clear = clear && fabsf(x - axis_fraction(low, span, ticks[j]) * (float)w) >= MIN_TICK_GAP;
                if (clear)
                    ticks[count++] = hz;
            }
        }
    } else {
        static const int steps[] = {100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000};
        int step = steps[0];
        for (size_t i = 0; i < sizeof steps / sizeof steps[0]; ++i) {
            step = steps[i];
            if (span / step <= 4)
                break;
        }
        for (int hz = 0; hz <= span && count < AXIS_LABELS; hz += step)
            ticks[count++] = hz;
    }
    for (int i = 0; i < AXIS_LABELS; ++i) {
        lv_obj_t *l = s_axis[i];
        if (i >= count) {
            lv_obj_set_hidden(l, true);
            continue;
        }
        const int hz = ticks[i];
        char t[12];
        if (hz >= 1000 && hz % 1000)
            snprintf(t, sizeof t, "%d.%dk", hz / 1000, hz % 1000 / 100);
        else if (hz >= 1000)
            snprintf(t, sizeof t, "%dk", hz / 1000);
        else
            snprintf(t, sizeof t, "%d", hz);
        ui_set_text(l, t);
        int x = pad + (int)(axis_fraction(low, span, hz) * (float)w) - 20;
        if (x < 0)
            x = 0;
        if (x > DOCK_W - 40)
            x = DOCK_W - 40;
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, x, -1);
        lv_obj_set_hidden(l, false);
    }
    ui_set_text(s_axis_unit, "Hz");
}

static void spectrum_event(lv_event_t *e)
{
    (void)e;
    show_dock(DOCK_TILES);
}

static void spectrum_panel(lv_obj_t *parent)
{
    s_spectrum = lv_obj_create(parent);
    lv_obj_remove_style_all(s_spectrum);
    lv_obj_set_size(s_spectrum, DOCK_W, DOCK_H);
    lv_obj_align(s_spectrum, LV_ALIGN_CENTER, 0, DOCK_Y);
    lv_obj_set_style_radius(s_spectrum, 12, 0);
    lv_obj_set_style_bg_color(s_spectrum, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(s_spectrum, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_spectrum, false);
    lv_obj_add_event_cb(s_spectrum, spectrum_draw, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(s_spectrum, spectrum_event, LV_EVENT_SHORT_CLICKED, NULL);
    // Frequency scale along the bottom; relabelled when the span changes.
    for (int i = 0; i < AXIS_LABELS; ++i) {
        s_axis[i] = label(s_spectrum, &lv_font_montserrat_14, COL_TEXT, "");
        lv_obj_set_width(s_axis[i], 40);
        lv_obj_set_style_text_align(s_axis[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_hidden(s_axis[i], true);
    }
    s_axis_unit = label(s_spectrum, &lv_font_montserrat_14, COL_TEXT, "");
    lv_obj_align(s_axis_unit, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_set_hidden(s_spectrum, true);
}

// New frame from the model: copy it and let peaks fall toward the bars.
static void update_spectrum(const knob_model_t *v)
{
    // While transmitting AetherSDR sends the transmit audio; draw it in red.
    if (v->transmitting != s_spectrum_tx) {
        s_spectrum_tx = v->transmitting;
        if (s_dock == DOCK_SPECTRUM)
            lv_obj_invalidate(s_spectrum);
    }
    if (v->spectrum_bars <= 0 || v->spectrum_ms == s_spectrum_stamp)
        return;
    if (!spectrum_live())
        s_live_since_ms = lv_tick_get();   // a new run of frames starts
    s_spectrum_stamp = v->spectrum_ms;
    s_spectrum_seen_ms = lv_tick_get();
    if (v->spectrum_span_hz > 0
        && (v->spectrum_span_hz != s_span_hz || v->spectrum_low_hz != s_low_hz)) {
        s_span_hz = v->spectrum_span_hz;
        s_low_hz = v->spectrum_low_hz;
        label_axis(s_low_hz, s_span_hz);
        memset(s_peaks, 0, sizeof s_peaks);   // old peaks belong to other frequencies
    }
    s_bar_count = v->spectrum_bars;
    for (int i = 0; i < s_bar_count; ++i) {
        s_bars[i] = v->spectrum[i];
        int p = s_peaks[i] - PEAK_DECAY;
        s_peaks[i] = (uint8_t)(s_bars[i] > p ? s_bars[i] : p);
    }
    if (s_dock == DOCK_SPECTRUM)
        lv_obj_invalidate(s_spectrum);
}

// The spectrum is the default once it has settled; the tiles take over if
// it stops, and give way to it again after DOCK_TILES_MS unused.
static bool s_spectrum_shown_once;

static void dock_tick(void)
{
    if (s_dock == DOCK_SPECTRUM && !spectrum_live()) {
        show_dock(DOCK_TILES);
    } else if (s_dock == DOCK_TILES && spectrum_settled() && s_mode == MODE_TUNE
               && (!s_spectrum_shown_once || lv_tick_get() - s_tiles_used_ms >= DOCK_TILES_MS)) {
        s_spectrum_shown_once = true;
        show_dock(DOCK_SPECTRUM);
    }
}

// ---------------------------------------------------------------- refresh

static void format_freq(char *buf, size_t cap, double mhz)
{
    long hz = lround(mhz * 1e6);
    snprintf(buf, cap, "%ld.%03ld.%03ld", hz / 1000000, (hz / 1000) % 1000, hz % 1000);
}

enum { TX_HIDDEN, TX_OTHER, TX_SLICE, TX_ON_AIR };

static void show_tx_badge(slice_view_t *v, int state)
{
    if (state == v->tx_state)
        return;
    v->tx_state = state;
    lv_obj_set_hidden(v->tx, state == TX_HIDDEN);
    lv_color_t edge = state == TX_OTHER ? COL_DIM : COL_LEVEL;
    lv_obj_set_style_border_color(v->tx, edge, 0);
    lv_obj_set_style_text_color(v->tx, state == TX_ON_AIR ? COL_TEXT : edge, 0);
    lv_obj_set_style_bg_opa(v->tx, state == TX_ON_AIR ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void show_slice(slice_view_t *v, const knob_slice_t *s, int index, bool transmitting)
{
    char buf[32];
    v->slice = s ? index : -1;
    if (!s) {
        ui_set_text(v->freq, v->top ? "--.---.---" : "");
        ui_set_text(v->mode, "");
        ui_set_text(v->filter, "");
        show_tx_badge(v, TX_HIDDEN);
        return;
    }
    format_freq(buf, sizeof buf, s->freq_mhz);
    ui_set_text(v->freq, buf);
    ui_set_textf(v->mode, "%c  %s", s->letter, s->mode);
    ui_set_textf(v->filter, "%d-%d  %s", s->filter_lo, s->filter_hi, s->rxant);
    show_tx_badge(v, !s->tx ? TX_OTHER : transmitting ? TX_ON_AIR : TX_SLICE);
    lv_obj_align_to(v->tx, v->mode, LV_ALIGN_OUT_LEFT_MID, -10, 0);
}

// Every frame: the S-meter bar and its peak marker, with AetherSDR's
// SmartMTR ballistics (meter_ballistics.c). A view that switches to another
// slice starts fresh, so one slice's history never shows on another.
static void meter_tick(slice_view_t *v, const knob_slice_t *s, int index, uint32_t now,
                       uint32_t elapsed)
{
    if (index != v->meter_slice) {
        v->meter_slice = index;
        meter_extremes_reset(&v->extremes);
        v->bar.target = v->bar.display = s ? smeter_fraction(s->level_dbm) : 0.0f;
    }
    if (s) {
        meter_smoother_set_target(&v->bar, smeter_fraction(s->level_dbm));
        meter_extremes_record(&v->extremes, s->level_dbm, now);
    } else {
        meter_smoother_set_target(&v->bar, 0.0f);
    }
    meter_smoother_tick(&v->bar, elapsed);
    meter_extremes_tick(&v->extremes, now, elapsed, v->bar.display, smeter_fraction);

    lv_arc_set_value(v->arc, (int32_t)lroundf(v->bar.display * 1000.0f));
    // Peak marker: a sliver two degrees wide, centered on the window maximum.
    int32_t a = (int32_t)lroundf(meter_angle(v->top, v->extremes.max_pos));
    if (a != v->peak_angle) {
        v->peak_angle = a;
        lv_arc_set_angles(v->peak, a - 1, a + 1);
    }
}

static void show_tiles(const knob_slice_t *s)
{
    for (int i = 0; i < 6; ++i) {
        tile_t *t = &s_tiles[i];
        bool on = false;
        if (!s) {
            ui_set_text(t->label, t->name);
        } else {
            switch (t->param) {
            case PARAM_AF:
                ui_set_textf(t->label, "AF\n%d", s->audio_level);
                break;
            case PARAM_AGC_T:
                ui_set_textf(t->label, "AGC %s\n%d", s->agc_mode,
                             strcmp(s->agc_mode, "off") == 0 ? kv_get_int(&s->kv, "agc_off_level")
                                                             : s->agc_threshold);
                on = strcmp(s->agc_mode, "off") != 0;
                break;
            case PARAM_FILTER_WIDTH:
                ui_set_textf(t->label, "FILT\n%d", s->filter_hi - s->filter_lo);
                break;
            case PARAM_NR_LEVEL:
                ui_set_textf(t->label, "NR\n%d", s->nr_level);
                on = s->nr;
                break;
            case PARAM_NB_LEVEL:
                ui_set_textf(t->label, "NB\n%d", s->nb_level);
                on = s->nb;
                break;
            case PARAM_ANF_LEVEL:
                ui_set_textf(t->label, "ANF\n%d", s->anf_level);
                on = s->anf;
                break;
            default:
                break;
            }
        }
        if (on != t->on) {
            t->on = on;
            lv_obj_set_style_bg_color(t->btn, on ? COL_TILE_ON : COL_TILE, 0);
        }
        bool selected = s_mode == MODE_PARAM && s_param == t->param;
        if (selected != t->selected) {
            t->selected = selected;
            lv_obj_set_style_border_width(t->btn, selected ? 3 : 0, 0);
        }
    }
}

static const char *status_text(link_state_t link)
{
    switch (link) {
    case LINK_NO_HOST:
        return "AetherKnob\n\nPlug into the PC running\nAetherSDR";
    case LINK_WAITING:
        return "Waiting for AetherSDR\n\nCTR2 Proxy applet:\nMode USB, pick AetherKnob,\npress Start";
    case LINK_RADIO:
        return "Radio connected\n\nWaiting for a SmartSDR\nor AetherSDR station";
    default:
        return "";
    }
}

static int other_slice(const knob_model_t *m, int focus)
{
    for (int i = 0; i < KNOB_MAX_SLICES; ++i) {
        const knob_slice_t *s = &m->slices[i];
        if (i != focus && s->in_use && s->client_handle == m->gui_handle)
            return i;
    }
    return -1;
}

static void handle_dial(const knob_model_t *m)
{
    int detents = board_dial_take_detents();
    bool down = board_dial_is_down();
    uint32_t now = lv_tick_get();
    bool held = false;
    if (down && !s_btn_was_down) {
        s_dial_down_ms = now;
        s_hold_fired = false;
    }
    if (down && !s_hold_fired && now - s_dial_down_ms >= DIAL_HOLD_MS) {
        s_hold_fired = true;
        held = true;
    }
    bool clicked = !down && s_btn_was_down && !s_hold_fired;
    s_btn_was_down = down;

    if (menu_is_open()) {
        if (detents)
            menu_dial_turn(detents);
        if (clicked)
            menu_dial_click();
        if (held)
            menu_dial_hold();
        return;
    }
    if (held && m->link == LINK_BOUND) {
        // Click-and-hold on the face: tune lock on the active slice.
        knob_action_t a = {.kind = ACT_MENU_TOGGLE, .item = menu_item_with_special(SPECIAL_LOCK)};
        if (a.item >= 0)
            usb_link_post_action(&a);
    }
    if (detents) {
        knob_action_t a = s_mode == MODE_TUNE
            ? (knob_action_t){.kind = ACT_TUNE, .arg = detents}
            : (knob_action_t){.kind = ACT_ADJUST, .param = s_param, .arg = detents};
        usb_link_post_action(&a);
    }
    if (clicked) {
        if (s_mode == MODE_PARAM) {
            s_mode = MODE_TUNE;           // a dial click leaves parameter mode
            s_seen_revision = UINT32_MAX;
        } else if (m->link == LINK_BOUND) {
            knob_action_t a = {.kind = ACT_STEP_NEXT};
            usb_link_post_action(&a);
        }
    }
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    model_lock();
    knob_model_t *m = model_get();
    bool changed = m->revision != s_seen_revision;
    if (changed) {
        s_snap = *m;
        s_seen_revision = m->revision;
    }
    model_unlock();

    handle_dial(&s_snap);
    const knob_model_t *v = &s_snap;
    update_spectrum(v);
    dock_tick();
    int focus = model_focus_slice(v);
    menu_update(v, focus, changed);

    // Meters animate every frame, between radio updates too.
    const uint32_t now = lv_tick_get();
    const uint32_t elapsed = s_last_frame_ms ? now - s_last_frame_ms : 0;
    s_last_frame_ms = now;
    const int other_meter = focus >= 0 ? other_slice(v, focus) : -1;
    meter_tick(&s_top, focus >= 0 ? &v->slices[focus] : NULL, focus, now, elapsed);
    meter_tick(&s_bottom, other_meter >= 0 ? &v->slices[other_meter] : NULL, other_meter, now,
               elapsed);
    if (!changed)
        return;

    bool bound = v->link == LINK_BOUND;
    lv_obj_set_hidden(s_status_panel, bound);
    ui_set_text(s_status_label, status_text(v->link));

    int other = focus >= 0 ? other_slice(v, focus) : -1;
    const knob_slice_t *fs = focus >= 0 ? &v->slices[focus] : NULL;
    show_slice(&s_top, fs, focus, v->transmitting);
    show_slice(&s_bottom, other >= 0 ? &v->slices[other] : NULL, other, v->transmitting);
    bool locked = fs && kv_get_int(&fs->kv, "lock");
    if (locked != s_locked_drawn) {
        s_locked_drawn = locked;
        lv_obj_set_style_text_color(s_step_label, locked ? COL_LEVEL : COL_DIM, 0);
    }
    if (locked)
        ui_set_text(s_step_label, "LOCKED");
    else if (fs)
        ui_set_textf(s_step_label, s_mode == MODE_TUNE ? "STEP %d Hz" : "DIAL ADJUSTS", fs->step);
    else
        ui_set_text(s_step_label, "");
    show_tiles(fs);

    ui_set_text(s_station_label, other < 0 && bound ? v->gui_station : "");

    gauge_set(&s_swr, v->swr < 1.0f ? 1.0f : v->swr);
    gauge_set(&s_comp, v->comp_db);
    gauge_set(&s_pwr, v->fwd_watts);
    gauge_set(&s_mic, v->mic_db);
}

// A swipe down anywhere on the face opens the menu.
static void screen_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (menu_is_open() || s_snap.link != LINK_BOUND)
        return;
    if (dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(indev);   // the swipe is not also a tile tap
        menu_open();
    } else if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        show_dock(s_dock == DOCK_TILES ? DOCK_SPECTRUM : DOCK_TILES);
    }
}

void ui_start(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *face = lv_obj_create(scr);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, 480, 480);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face, COL_FACE, 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(face, false);
    lv_obj_center(face);

    slice_view(face, &s_top, true);
    slice_view(face, &s_bottom, false);
    gauge(face, &s_swr, 84, 208, 1.0f, 3.0f, "SWR", "1", "3");
    gauge(face, &s_comp, 84, 280, 0.0f, 25.0f, "COMP", "0", "25");
    gauge(face, &s_pwr, 396, 208, 0.0f, 100.0f, "PWR", "0", "100");
    gauge(face, &s_mic, 396, 280, -40.0f, 0.0f, "MIC", "-40", "0");
    s_tiles_box = lv_obj_create(face);
    lv_obj_remove_style_all(s_tiles_box);
    lv_obj_set_size(s_tiles_box, DOCK_W + 4, DOCK_H);
    lv_obj_align(s_tiles_box, LV_ALIGN_CENTER, 0, DOCK_Y);
    lv_obj_set_scrollable(s_tiles_box, false);
    tiles(s_tiles_box);
    spectrum_panel(face);

    s_step_label = label(face, &lv_font_montserrat_14, COL_DIM, "");
    lv_obj_align(s_step_label, LV_ALIGN_CENTER, 0, 64);
    s_station_label = label(face, &lv_font_montserrat_20, COL_DIM, "");
    lv_obj_align(s_station_label, LV_ALIGN_CENTER, 0, 110);

    menu_create(scr);
    lv_obj_add_event_cb(scr, screen_gesture, LV_EVENT_GESTURE, NULL);

    s_status_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_status_panel);
    lv_obj_set_size(s_status_panel, 480, 480);
    lv_obj_set_style_radius(s_status_panel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_panel, COL_FACE, 0);
    lv_obj_set_style_bg_opa(s_status_panel, LV_OPA_90, 0);
    lv_obj_center(s_status_panel);
    s_status_label = label(s_status_panel, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_status_label);

    lv_timer_create(refresh, 33, NULL);
    refresh(NULL);
}

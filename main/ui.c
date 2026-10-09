#include "ui.h"

#include "board.h"
#include "model.h"
#include "usb_link.h"

#include <math.h>
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

// S-meter geometry: S0..S9 fill 60% of the arc, S9..S9+60 the rest.
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
} slice_view_t;

typedef struct {
    lv_obj_t *needle;
    lv_point_precise_t pts[2];
    int cx, cy, len;
    float lo, hi;
} gauge_t;

typedef struct {
    lv_obj_t *btn;
    lv_obj_t *label;
    knob_param_t param;
    knob_action_kind_t toggle;   // long-press action
    const char *name;
} tile_t;

static slice_view_t s_top, s_bottom;
static gauge_t s_swr, s_comp, s_pwr, s_mic;
static tile_t s_tiles[6];
static lv_obj_t *s_status_panel, *s_status_label, *s_step_label, *s_station_label;
static knob_mode_t s_mode = MODE_TUNE;
static knob_param_t s_param = PARAM_AF;
static uint32_t s_seen_revision = UINT32_MAX;
static bool s_btn_was_down;
static knob_model_t s_snap;

// ---------------------------------------------------------------- geometry

// Fraction 0..1 along the S-meter for a level in dBm.
static float smeter_fraction(float dbm)
{
    const float s0 = -127.0f, s9 = -73.0f, top = -13.0f;  // S9+60
    if (dbm <= s0)
        return 0.0f;
    if (dbm <= s9)
        return 0.6f * (dbm - s0) / (s9 - s0);
    if (dbm >= top)
        return 1.0f;
    return 0.6f + 0.4f * (dbm - s9) / (top - s9);
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

// Ticks and labels: S1..S9 odd units, then +20/+40/+60, like SmartMTR.
static void meter_scale(lv_obj_t *parent, bool top)
{
    static const struct { float dbm; const char *text; } marks[] = {
        {-121, "1"}, {-109, "3"}, {-97, "5"}, {-85, "7"}, {-73, "9"},
        {-53, "+20"}, {-33, "+40"}, {-13, "+60"},
    };
    static lv_point_precise_t tick_pts[2][16][2];
    // A tick per S unit to S9 (long on odd units), then every 10 dB to +60
    // (long on the labelled 20s).
    for (int i = 1; i < 16; ++i) {
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

static void slice_view(lv_obj_t *parent, slice_view_t *v, bool top)
{
    v->top = top;
    v->arc = meter_arc(parent, top, ARC_WIDTH, true, COL_LEVEL);
    // Peak-hold: a thin white sliver drawn by a second, track-less arc.
    v->peak = meter_arc(parent, top, ARC_WIDTH + 6, false, lv_color_white());
    lv_obj_set_style_arc_rounded(v->peak, false, LV_PART_INDICATOR);
    meter_scale(parent, top);

    // Offsets from the centre. The top block reads mode, frequency, filter
    // downwards; the bottom block mirrors it.
    v->freq = label(parent, top ? &lv_font_montserrat_40 : &lv_font_montserrat_28, COL_TEXT, "--.---.---");
    lv_obj_align(v->freq, LV_ALIGN_CENTER, 0, top ? -92 : 110);
    v->mode = label(parent, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_align(v->mode, LV_ALIGN_CENTER, 0, top ? -134 : 142);
    v->tx = label(parent, &lv_font_montserrat_14, COL_LEVEL, "TX");
    lv_obj_set_style_border_width(v->tx, 1, 0);
    lv_obj_set_style_border_color(v->tx, COL_LEVEL, 0);
    lv_obj_set_style_pad_hor(v->tx, 4, 0);
    lv_obj_set_hidden(v->tx, true);
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
    g->pts[0] = (lv_point_precise_t){g->cx, g->cy};
    g->pts[1] = (lv_point_precise_t){x, y};
    lv_line_set_points(g->needle, g->pts, 2);
}

// ---------------------------------------------------------------- tiles

static void select_param(knob_param_t p)
{
    if (s_mode == MODE_PARAM && s_param == p) {
        s_mode = MODE_TUNE;               // tap the selected tile again: back to VFO
    } else {
        s_mode = MODE_PARAM;
        s_param = p;
    }
    s_seen_revision = UINT32_MAX;         // force a redraw of the highlight
}

static void tile_event(lv_event_t *e)
{
    tile_t *t = lv_event_get_user_data(e);
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
        {"AF", PARAM_AF, ACT_AGC_NEXT},       // AF has no toggle; long-press does AGC mode
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
        lv_obj_set_size(t->btn, 74, 46);
        lv_obj_align(t->btn, LV_ALIGN_CENTER, (i % 3 - 1) * 80, i < 3 ? -20 : 28);
        lv_obj_set_style_bg_color(t->btn, COL_TILE, 0);
        lv_obj_set_style_radius(t->btn, 10, 0);
        lv_obj_set_style_shadow_width(t->btn, 0, 0);
        lv_obj_set_style_border_color(t->btn, COL_SELECT, 0);
        lv_obj_add_event_cb(t->btn, tile_event, LV_EVENT_ALL, t);
        t->label = label(t->btn, &lv_font_montserrat_14, COL_TEXT, t->name);
        lv_obj_set_style_text_align(t->label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(t->label);
    }
}

// ---------------------------------------------------------------- refresh

static void format_freq(char *buf, size_t cap, double mhz)
{
    long hz = lround(mhz * 1e6);
    snprintf(buf, cap, "%ld.%03ld.%03ld", hz / 1000000, (hz / 1000) % 1000, hz % 1000);
}

static void show_slice(slice_view_t *v, const knob_slice_t *s, bool transmitting)
{
    char buf[32];
    if (!s) {
        lv_arc_set_value(v->arc, 0);
        lv_arc_set_value(v->peak, 0);
        lv_label_set_text(v->freq, v->top ? "--.---.---" : "");
        lv_label_set_text(v->mode, "");
        lv_label_set_text(v->filter, "");
        lv_obj_set_hidden(v->tx, true);
        return;
    }
    float f = smeter_fraction(s->level_dbm);
    float pk = smeter_fraction(s->level_peak_dbm);
    lv_arc_set_value(v->arc, (int32_t)(f * 1000));
    // Peak-hold sliver, two degrees wide, centred on the peak.
    int32_t a = (int32_t)lroundf(meter_angle(v->top, pk));
    lv_arc_set_angles(v->peak, a - 1, a + 1);
    format_freq(buf, sizeof buf, s->freq_mhz);
    lv_label_set_text(v->freq, buf);
    lv_label_set_text_fmt(v->mode, "%c  %s", s->letter, s->mode);
    lv_label_set_text_fmt(v->filter, "%d-%d  %s", s->filter_lo, s->filter_hi, s->rxant);
    if (s->tx) {
        lv_obj_set_hidden(v->tx, false);
        lv_obj_align_to(v->tx, v->mode, LV_ALIGN_OUT_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(v->tx, transmitting ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(v->tx, COL_LEVEL, 0);
        lv_obj_set_style_text_color(v->tx, transmitting ? COL_TEXT : COL_LEVEL, 0);
    } else {
        lv_obj_set_hidden(v->tx, true);
    }
}

static void show_tiles(const knob_slice_t *s)
{
    for (int i = 0; i < 6; ++i) {
        tile_t *t = &s_tiles[i];
        bool on = false;
        if (!s) {
            lv_label_set_text(t->label, t->name);
        } else {
            switch (t->param) {
            case PARAM_AF:
                lv_label_set_text_fmt(t->label, "AF\n%d", s->audio_level);
                break;
            case PARAM_AGC_T:
                lv_label_set_text_fmt(t->label, "AGC %s\n%d", s->agc_mode, s->agc_threshold);
                on = strcmp(s->agc_mode, "off") != 0;
                break;
            case PARAM_FILTER_WIDTH:
                lv_label_set_text_fmt(t->label, "FILT\n%d", s->filter_hi - s->filter_lo);
                break;
            case PARAM_NR_LEVEL:
                lv_label_set_text_fmt(t->label, "NR\n%d", s->nr_level);
                on = s->nr;
                break;
            case PARAM_NB_LEVEL:
                lv_label_set_text_fmt(t->label, "NB\n%d", s->nb_level);
                on = s->nb;
                break;
            case PARAM_ANF_LEVEL:
                lv_label_set_text_fmt(t->label, "ANF\n%d", s->anf_level);
                on = s->anf;
                break;
            default:
                break;
            }
        }
        lv_obj_set_style_bg_color(t->btn, on ? COL_TILE_ON : COL_TILE, 0);
        bool selected = s_mode == MODE_PARAM && s_param == t->param;
        lv_obj_set_style_border_width(t->btn, selected ? 3 : 0, 0);
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

static void handle_knob(const knob_model_t *m)
{
    int detents = board_knob_take_detents();
    if (detents) {
        knob_action_t a = s_mode == MODE_TUNE
            ? (knob_action_t){.kind = ACT_TUNE, .arg = detents}
            : (knob_action_t){.kind = ACT_ADJUST, .param = s_param, .arg = detents};
        usb_link_post_action(&a);
    }
    bool down = board_knob_pressed();
    if (down && !s_btn_was_down) {
        if (s_mode == MODE_PARAM) {
            s_mode = MODE_TUNE;           // push leaves parameter mode
            s_seen_revision = UINT32_MAX;
        } else if (m->link == LINK_BOUND) {
            knob_action_t a = {.kind = ACT_STEP_NEXT};
            usb_link_post_action(&a);
        }
    }
    s_btn_was_down = down;
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

    handle_knob(&s_snap);
    if (!changed)
        return;

    const knob_model_t *v = &s_snap;
    bool bound = v->link == LINK_BOUND;
    lv_obj_set_hidden(s_status_panel, bound);
    lv_label_set_text(s_status_label, status_text(v->link));

    int focus = model_focus_slice(v);
    int other = focus >= 0 ? other_slice(v, focus) : -1;
    const knob_slice_t *fs = focus >= 0 ? &v->slices[focus] : NULL;
    show_slice(&s_top, fs, v->transmitting);
    show_slice(&s_bottom, other >= 0 ? &v->slices[other] : NULL, v->transmitting);
    if (fs)
        lv_label_set_text_fmt(s_step_label, s_mode == MODE_TUNE ? "STEP %d Hz" : "KNOB ADJUSTS", fs->step);
    else
        lv_label_set_text(s_step_label, "");
    show_tiles(fs);

    lv_label_set_text(s_station_label, other < 0 && bound ? v->gui_station : "");

    gauge_set(&s_swr, v->swr < 1.0f ? 1.0f : v->swr);
    gauge_set(&s_comp, v->comp_db);
    gauge_set(&s_pwr, v->fwd_watts);
    gauge_set(&s_mic, v->mic_db);
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
    tiles(face);

    s_step_label = label(face, &lv_font_montserrat_14, COL_DIM, "");
    lv_obj_align(s_step_label, LV_ALIGN_CENTER, 0, 60);
    s_station_label = label(face, &lv_font_montserrat_20, COL_DIM, "");
    lv_obj_align(s_station_label, LV_ALIGN_CENTER, 0, 110);

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

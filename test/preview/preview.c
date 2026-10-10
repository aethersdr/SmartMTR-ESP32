// Renders the AetherKnob face on the host with sample radio data, so layout
// changes can be checked without the board. Build: make -C test/preview
#include "../../main/board.h"
#include "../../main/menu.h"
#include "../../main/menu_items.h"
#include "../../main/model.h"
#include "../../main/ui.h"
#include "../../main/usb_link.h"

#include <math.h>
#include "../../main/ctr2_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int board_dial_take_detents(void) { return 0; }
bool board_dial_is_down(void) { return false; }
// Actions the UI posted, for the touch test.
static knob_action_t s_actions[64];
static int s_action_count;
void usb_link_post_action(const knob_action_t *a)
{
    if (s_action_count < 64)
        s_actions[s_action_count++] = *a;
}

// A scripted finger.
static lv_point_t s_finger;
static bool s_finger_down;
static void finger_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = s_finger;
    data->state = s_finger_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t s_fb[480 * 480];

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    for (int y = a->y1; y <= a->y2; ++y)
        memcpy(&s_fb[y * 480 + a->x1], px + (size_t)(y - a->y1) * (size_t)(a->x2 - a->x1 + 1) * 4,
               (size_t)(a->x2 - a->x1 + 1) * 4);
    lv_display_flush_ready(d);
}

static void slice(knob_slice_t *s, char letter, double mhz, const char *mode, int lo, int hi,
                  float dbm, float peak, bool active, bool tx)
{
    *s = (knob_slice_t){.in_use = true, .letter = letter, .freq_mhz = mhz, .filter_lo = lo,
                        .filter_hi = hi, .active = active, .client_handle = 0x33333333,
                        .audio_level = 50, .agc_threshold = 65, .nr = true, .nr_level = 40,
                        .nb_level = 50, .anf_level = 30, .step = 100, .tx = tx,
                        .level_dbm = dbm};
    (void)peak;   // the peak marker now comes from the SmartMTR window, not the model
    snprintf(s->mode, sizeof s->mode, "%s", mode);
    snprintf(s->agc_mode, sizeof s->agc_mode, "med");
    snprintf(s->rxant, sizeof s->rxant, "ANT2");
}

// Speech through a ~3 kHz SSB filter, sampled at each bar's centre on the
// log axis AetherSDR uses (bar edges from ctr2_spectrum_band_edge).
static void voice_spectrum(knob_model_t *m, int low, int span)
{
    m->spectrum_bars = 32;
    m->spectrum_low_hz = low;
    m->spectrum_span_hz = span;
    for (int i = 0; i < 32; ++i) {
        float f = sqrtf(ctr2_spectrum_band_edge(low, span, 32, i)
                        * ctr2_spectrum_band_edge(low, span, 32, i + 1));
        float db = -78.0f;                                    // noise floor
        if (f > 150.0f && f < 2900.0f) {
            db = -30.0f - 9.0f * fabsf(log2f(f / 450.0f)) * (f > 450.0f ? 0.6f : 1.6f);
            db += 3.0f * sinf((float)i * 1.7f);              // formant ripple
        }
        if (f >= 2900.0f)
            db = -62.0f - (f - 2900.0f) / 120.0f;             // filter skirt
        float level = (db + 90.0f) * 255.0f / 90.0f;
        m->spectrum[i] = (uint8_t)(level < 0 ? 0 : level > 255 ? 255 : level);
    }
}

static void put(knob_kv_t *kv, bool slice, const char *key, const char *value)
{
    kv_put(kv, menu_tracked_key(slice, key), value);
}

// Lets animations and timers run for `ms` of simulated time.
static void run_for(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

// The control of `type` in the menu row labelled `name`.
static lv_obj_t *find_in_row(lv_obj_t *obj, const char *name, const lv_obj_class_t *type)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t *c = lv_obj_get_child(obj, (int32_t)i);
        if (lv_obj_check_type(c, &lv_label_class) && strcmp(lv_label_get_text(c), name) == 0) {
            for (uint32_t j = 0; j < lv_obj_get_child_count(obj); ++j) {
                lv_obj_t *d = lv_obj_get_child(obj, (int32_t)j);
                if (lv_obj_check_type(d, type))
                    return d;
            }
        }
        lv_obj_t *found = find_in_row(c, name, type);
        if (found)
            return found;
    }
    return NULL;
}

static lv_point_t centre_of(lv_obj_t *obj)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return (lv_point_t){(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
}

static void drag(lv_point_t from, int dx, int dy)
{
    s_finger = from;
    s_finger_down = true;
    run_for(60);
    for (int k = 1; k <= 10; ++k) {
        s_finger.x = from.x + dx * k / 10;
        s_finger.y = from.y + dy * k / 10;
        run_for(40);
    }
    s_finger_down = false;
    run_for(100);
}

// A short tap, well under LVGL's long-press time.
static void tap(lv_point_t at)
{
    s_finger = at;
    s_finger_down = true;
    run_for(80);
    s_finger_down = false;
    run_for(100);
}

static int count_kind(knob_action_kind_t kind)
{
    int n = 0;
    for (int i = 0; i < s_action_count; ++i)
        n += s_actions[i].kind == kind;
    return n;
}

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "touch test FAILED: %s\n", what);                  \
            return 1;                                                           \
        }                                                                       \
        printf("touch test ok: %s\n", what);                                   \
    } while (0)

static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t *c = lv_obj_get_child(obj, (int32_t)i);
        if (lv_obj_check_type(c, &lv_label_class) && strcmp(lv_label_get_text(c), text) == 0)
            return c;
        lv_obj_t *found = find_label(c, text);
        if (found)
            return found;
    }
    return NULL;
}

// A new spectrum frame, as AetherSDR sends 20 a second.
static void fresh_spectrum(void)
{
    knob_model_t *m = model_get();
    m->spectrum_ms++;
    m->revision++;
}

// Drives the face and the menu with a scripted finger.
static int touch_test(void)
{
    lv_indev_t *finger = lv_indev_create();
    lv_indev_set_type(finger, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(finger, finger_read);

    // Centre dock: spectrum by default, a tap shows the tiles, a sideways
    // swipe brings the spectrum back.
    fresh_spectrum();
    run_for(100);
    lv_obj_t *af = find_label(lv_screen_active(), "AF\n50");
    CHECK(af && !lv_obj_is_visible(af), "the dock opens on the spectrum, tiles hidden");
    fresh_spectrum();
    tap((lv_point_t){240, 250});
    CHECK(lv_obj_is_visible(af), "a tap on the spectrum shows the tiles");
    CHECK(s_action_count == 0, "...and is not also a tile tap");
    fresh_spectrum();
    drag((lv_point_t){150, 335}, 180, 0);
    fresh_spectrum();
    run_for(60);
    CHECK(!lv_obj_is_visible(af), "a sideways swipe switches back to the spectrum");

    menu_open();
    run_for(400);

    lv_obj_t *rit = find_in_row(lv_screen_active(), "RIT", &lv_slider_class);
    CHECK(rit != NULL, "RIT row has a fader");
    lv_obj_t *list = lv_obj_get_parent(lv_obj_get_parent(rit));
    int before = lv_slider_get_value(rit);

    s_action_count = 0;
    drag(centre_of(rit), 4, -140);
    CHECK(lv_obj_get_scroll_y(list) > 40, "a vertical drag starting on a fader scrolls the list");
    CHECK(count_kind(ACT_MENU_SET_LEVEL) == 0, "...and does not move the fader");
    CHECK(lv_slider_get_value(rit) == before, "...and the fader shows the radio's value");

    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    run_for(100);
    s_action_count = 0;
    drag(centre_of(rit), 90, 3);
    CHECK(count_kind(ACT_MENU_SET_LEVEL) > 0, "a sideways drag on a fader sets the level");
    knob_action_t last = s_actions[s_action_count - 1];
    CHECK(last.arg > before && last.arg % 10 == 0, "...upwards, snapped to the 10 Hz step");

    lv_obj_t *sq = find_in_row(lv_screen_active(), "Squelch", &lv_switch_class);
    CHECK(sq != NULL, "Squelch row has a switch");
    lv_obj_scroll_to_view(lv_obj_get_parent(sq), LV_ANIM_OFF);
    run_for(100);
    s_action_count = 0;
    tap(centre_of(sq));
    CHECK(count_kind(ACT_MENU_SET_SWITCH) == 1 && s_actions[0].arg == 1, "tapping a switch turns it on");

    // Dial only: the highlight starts on Mode. Click edits it, turning
    // previews the next mode without sending, a click sends it.
    menu_close();
    run_for(400);
    menu_open();
    run_for(400);
    s_action_count = 0;
    menu_dial_click();
    menu_dial_turn(1);
    run_for(100);
    CHECK(s_action_count == 0, "dial: turning a list row in edit only previews");
    menu_dial_click();
    CHECK(count_kind(ACT_MENU_CHOOSE) == 1 && s_actions[0].arg == 2, "dial: a click sends the previewed mode (AM)");
    menu_dial_click();                  // Mode into edit again
    menu_dial_turn(2);                  // preview
    run_for(100);
    menu_dial_hold();                   // back out
    run_for(1000);                      // past the rest time that would send it
    CHECK(count_kind(ACT_MENU_CHOOSE) == 1, "dial: click-and-hold drops a preview instead of sending it");
    return 0;
}

int main(int argc, char **argv)
{
    const char *state = argc > 2 ? argv[2] : "bound";
    bool bound = strcmp(state, "waiting") != 0;
    knob_model_t *m = model_get();
    if (!bound) {
        m->link = LINK_WAITING;
    } else {
        m->link = LINK_BOUND;
        m->gui_handle = 0x33333333;
        snprintf(m->gui_station, sizeof m->gui_station, "Arch");
        slice(&m->slices[0], 'A', 14.1725, "USB", 50, 3000, -95.0f, -75.0f, true, true);
        slice(&m->slices[1], 'B', 14.323, "USB", 100, 2800, -98.0f, -69.0f, false, false);
        m->swr = 1.4f; m->comp_db = 8.0f; m->fwd_watts = 0.0f; m->mic_db = -22.0f;
        knob_kv_t *kv = &m->slices[0].kv;
        put(kv, true, "mode", "USB");
        put(kv, true, "mode_list", "LSB,USB,AM,CW,DIGL,DIGU,FM");
        put(kv, true, "rxant", "ANT2");
        put(kv, true, "ant_list", "ANT1,ANT2,RX_A,XVTR");
        put(kv, true, "rit_on", "1");
        put(kv, true, "rit_freq", "-120");
        put(kv, true, "agc_mode", "med");
        put(kv, true, "agc_threshold", "65");
        put(kv, true, "agc_off_level", "40");
        put(kv, true, "squelch", "0");
        put(kv, true, "squelch_level", "20");
        put(kv, true, "nrl", "1");
        put(kv, true, "lms_nr_level", "35");
        put(kv, true, "anf", "0");
        put(kv, true, "wnb", "0");
        put(kv, true, "wnb_level", "50");
        put(kv, true, "audio_mute", "0");
        put(kv, true, "audio_pan", "50");
        put(kv, true, "diversity", "0");
        put(kv, true, "dax", "1");
        put(kv, true, "lock", "0");
        put(kv, true, "txant", "ANT1");
        put(kv, true, "tx_ant_list", "ANT1,ANT2");
        put(kv, true, "xit_on", "0");
        put(kv, true, "xit_freq", "0");
        put(&m->tx_kv, false, "rfpower", "100");
        put(&m->tx_kv, false, "tunepower", "10");
        put(&m->tx_kv, false, "mic_level", "45");
        put(&m->tx_kv, false, "compander", "1");
        put(&m->tx_kv, false, "compander_level", "60");
        put(&m->tx_kv, false, "sb_monitor", "0");
        put(&m->tx_kv, false, "mon_gain_sb", "50");
        voice_spectrum(m, 66, 4000);
        m->spectrum_ms = 1;
    }
    m->revision = 1;

    lv_init();
    lv_display_t *d = lv_display_create(480, 480);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    static uint8_t buf[480 * 480 * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    ui_start(d);
    // The spectrum takes over once frames have arrived for 1.5 s.
    for (int t = 0; t < 1700; t += 50) {
        model_get()->spectrum_ms++;
        model_get()->revision++;
        run_for(50);
    }
    if (strcmp(state, "tx") == 0) {
        m->transmitting = true;
        m->fwd_watts = 85.0f;
        m->spectrum_ms += 10;
        m->revision++;
        run_for(100);
    }
    if (strcmp(state, "live") == 0) {
        // A frame captured off the USB bus from AetherSDR (usbmon), passband
        // 100-4172 Hz: low 83, span 5000, content to the 3870-4398 Hz bar.
        static const uint8_t frame[32] = {79, 122, 138, 134, 130, 120, 123, 103, 140, 125, 111,
                                          122, 131, 111, 118, 118, 114, 122, 103, 106, 118, 115,
                                          113, 103, 104, 120, 122, 103, 102, 75, 105, 0};
        memcpy(m->spectrum, frame, sizeof frame);
        m->spectrum_bars = 32;
        m->spectrum_low_hz = 83;
        m->spectrum_span_hz = 5000;
        m->spectrum_ms += 10;
        m->revision++;
        run_for(100);
    }
    if (strcmp(state, "rowtest") == 0) {
        // Every setting row in the menu is the same height (headers excepted).
        menu_open();
        run_for(400);
        int heights[64], n = 0;
        lv_obj_t *list = NULL;
        lv_obj_t *mute = find_in_row(lv_screen_active(), "Mute", &lv_switch_class);
        if (mute)
            list = lv_obj_get_parent(lv_obj_get_parent(mute));
        for (uint32_t i = 0; list && i < lv_obj_get_child_count(list) && n < 64; ++i) {
            lv_obj_t *row = lv_obj_get_child(list, (int32_t)i);
            if (lv_obj_get_height(row) > 32)       // headers are 30 px
                heights[n++] = lv_obj_get_height(row);
        }
        bool same = n > 10;
        for (int i = 1; i < n; ++i)
            same = same && heights[i] == heights[0];
        printf("rowtest: %d setting rows, first %d px\n", n, n ? heights[0] : 0);
        if (!same) {
            fprintf(stderr, "rowtest FAILED: setting rows differ in height\n");
            return 1;
        }
        printf("rowtest ok: every setting row is the same height\n");
        return 0;
    }
    if (strcmp(state, "geomtest") == 0) {
        // Every bar full: the last bar must reach the right end of the axis
        // area (DOCK_W - pad), where the scale's top frequency is drawn.
        memset(m->spectrum, 255, 32);
        m->spectrum_bars = 32;
        m->spectrum_ms += 10;
        m->revision++;
        run_for(100);
        lv_refr_now(d);
        // Dock: 212 px wide, centred on x = 240; bars end at its right edge - 10.
        const int right = 240 + 212 / 2 - 10, y = 240 + 8;   // dock centre row
        uint32_t px_in = s_fb[y * 480 + right - 3], px_out = s_fb[y * 480 + right + 4];
        int in_r = (px_in >> 16) & 0xFF, in_g = (px_in >> 8) & 0xFF, in_b = px_in & 0xFF;
        int out_b = px_out & 0xFF, out_g = (px_out >> 8) & 0xFF;
        bool bar_at_edge = in_g > 150 && in_b > 200 && in_r < 120;     // the cyan bar colour
        bool panel_beyond = !(out_g > 150 && out_b > 200);
        printf("geomtest: pixel at x=%d is %06x, at x=%d is %06x\n", right - 3, px_in & 0xFFFFFF,
               right + 4, px_out & 0xFFFFFF);
        if (!bar_at_edge || !panel_beyond) {
            fprintf(stderr, "geomtest FAILED: the bars do not end at the axis's right edge\n");
            return 1;
        }
        printf("geomtest ok: the last bar ends at the axis's right edge\n");
        return 0;
    }
    int span = strcmp(state, "span600") == 0 ? 600 : strcmp(state, "span12k") == 0 ? 12000 : 0;
    if (span) {
        voice_spectrum(m, span == 600 ? 20 : 100, span);
        m->spectrum_ms += 10;
        m->revision++;
        run_for(100);
    }
    if (strcmp(state, "peaks") == 0) {
        // A second frame with one bar fallen, to show its peak-hold mark.
        m->spectrum[5] = 120;
        m->spectrum_ms = 2;
        m->revision++;
        run_for(100);
    }
    if (strcmp(state, "touchtest") == 0)
        return touch_test();
    if (strncmp(state, "menu", 4) == 0) {
        menu_open();
        run_for(400);
        // Turning moves the highlight down the list; menu_edit then clicks
        // the dial to put the highlighted row (AGC) into edit.
        int rows = strcmp(state, "menu_tx") == 0 ? 14 : strcmp(state, "menu_mid") == 0 ? 3
                 : strcmp(state, "menu_edit") == 0 ? 6 : 0;
        if (rows) {
            menu_dial_turn(rows);
            run_for(600);
        }
        if (strcmp(state, "menu_edit") == 0) {
            menu_dial_click();
            run_for(100);
        }
    }
    lv_refr_now(d);

    FILE *f = fopen(argc > 1 ? argv[1] : "face.ppm", "wb");
    fprintf(f, "P6\n480 480\n255\n");
    for (int i = 0; i < 480 * 480; ++i) {
        uint8_t rgb[3] = {(uint8_t)(s_fb[i] >> 16), (uint8_t)(s_fb[i] >> 8), (uint8_t)s_fb[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

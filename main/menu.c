#include "menu.h"

#include "flex_api.h"
#include "menu_items.h"
#include "ui.h"
#include "usb_link.h"

#include <stdio.h>
#include <string.h>

#define COL_FACE    lv_color_hex(0x07131D)
#define COL_ROW     lv_color_hex(0x0E2A3B)
#define COL_ROW_ON  lv_color_hex(0x1C6E99)
#define COL_TRACK   lv_color_hex(0x24485E)
#define COL_FILL    lv_color_hex(0x37C8F5)
#define COL_TEXT    lv_color_hex(0xE8EEF2)
#define COL_DIM     lv_color_hex(0x6F8796)
#define COL_HEAD    lv_color_hex(0x37C8F5)
#define COL_SELECT  lv_color_hex(0xFFD60A)

// Every setting row is the same height, fader or not, so each one is an
// equally easy target on the touchscreen. Rows without a fader centre their
// name and switch in it.
#define ROW_HEIGHT      74
#define IDLE_CLOSE_MS   8000
#define CHOICE_SEND_MS  700    // a previewed choice is sent once the dial rests this long

typedef struct {
    lv_obj_t *row;
    lv_obj_t *name;
    lv_obj_t *value;
    lv_obj_t *fader;    // levels and lists; NULL on on/off rows
    lv_obj_t *sw;       // on/off rows and rows with a switch
    lv_obj_t *chip;     // LEVEL_CYCLE: the mode, tap to step it
    lv_obj_t *chip_label;
    bool on;            // last drawn state, so styles are only touched on change
    int frame;          // last drawn outline: FRAME_*
    int fader_max;      // last range set on the fader
    bool fader_enabled;
} row_t;

enum { FRAME_NONE, FRAME_HIGHLIGHT, FRAME_EDIT };

static row_t s_rows[MENU_ITEM_MAX];
static lv_obj_t *s_menu, *s_list, *s_title;
static bool s_open;
// Dial navigation: turning moves the highlight; a click puts the highlighted
// row into edit (s_selected), where turning changes its value. A tap does
// both at once. Faders and switches act directly by touch.
static int s_highlight = -1;
static int s_selected = -1;
static int s_pending = -1;           // previewed choice index, -1 when none
static int s_pending_row = -1;       // row the preview belongs to (dial or fader)
static uint32_t s_pending_ms;
static uint32_t s_dial_ms;
static const knob_model_t *s_model;
static int s_focus = -1;

static uint32_t now_ms(void) { return lv_tick_get(); }

static const knob_slice_t *focus_slice(void)
{
    return s_model && s_focus >= 0 ? &s_model->slices[s_focus] : NULL;
}

static void post(knob_action_kind_t kind, int item, int arg)
{
    knob_action_t a = {.kind = kind, .item = item, .arg = arg};
    usb_link_post_action(&a);
}

static bool is_choice(const menu_item_t *it)
{
    return it->kind == ITEM_CHOICE || it->kind == ITEM_ONE_OF;
}

static bool has_fader(const menu_item_t *it)
{
    return it->kind != ITEM_HEADER && it->kind != ITEM_TOGGLE;
}

static bool has_switch(const menu_item_t *it)
{
    return it->kind == ITEM_TOGGLE || it->kind == ITEM_LEVEL_TOGGLE;
}

// ---------------------------------------------------------------- drawing

static void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (enabled)
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else
        lv_obj_add_state(obj, LV_STATE_DISABLED);
}

// The fader follows the radio, except while a finger is on it.
static void draw_fader(int i)
{
    const menu_item_t *it = &g_menu_items[i];
    row_t *r = &s_rows[i];
    const knob_slice_t *s = focus_slice();
    if (!r->fader || lv_obj_has_state(r->fader, LV_STATE_PRESSED))
        return;
    int max, value;
    bool known;
    if (is_choice(it)) {
        max = menu_choice_count(it, s) - 1;
        value = i == s_pending_row && s_pending >= 0 ? s_pending
                                                    : menu_choice_current(it, s, s_model);
        known = max > 0 && value >= 0;
    } else {
        max = it->max;
        known = s_model && menu_item_level(it, s, s_model, &value);
    }
    if (known && max != r->fader_max) {
        r->fader_max = max;
        lv_slider_set_range(r->fader, is_choice(it) ? 0 : it->min, max);
    }
    if (known)
        lv_slider_set_value(r->fader, value, LV_ANIM_OFF);
    if (known != r->fader_enabled) {
        r->fader_enabled = known;
        set_enabled(r->fader, known);
    }
}

static void draw_row(int i)
{
    const menu_item_t *it = &g_menu_items[i];
    row_t *r = &s_rows[i];
    const knob_slice_t *s = focus_slice();
    lv_obj_set_hidden(r->row, !menu_item_visible(it, s));
    if (it->kind == ITEM_HEADER)
        return;

    char buf[40];
    bool previewing = i == s_pending_row && s_pending >= 0;
    buf[0] = '\0';
    if (previewing) {
        menu_choice_label(it, s, s_pending, buf, sizeof buf);
    } else if (s_model && it->kind == ITEM_LEVEL_CYCLE) {
        int v;
        if (menu_item_level(it, s, s_model, &v))
            snprintf(buf, sizeof buf, "%d", v);
        char mode[16];
        int c = 0;
        menu_item_text(it, s, s_model, mode, sizeof mode);   // "Med  65"
        while (mode[c] && mode[c] != ' ')
            ++c;
        mode[c] = '\0';
        ui_set_text(r->chip_label, mode);
    } else if (s_model && it->kind != ITEM_TOGGLE) {
        menu_item_text(it, s, s_model, buf, sizeof buf);
        // The switch shows on/off; keep just the level beside it.
        if (it->kind == ITEM_LEVEL_TOGGLE) {
            char *gap = strstr(buf, "  ");
            if (gap)
                memmove(buf, gap + 2, strlen(gap + 2) + 1);
        }
    }
    ui_set_text(r->value, buf);
    lv_obj_set_style_text_color(r->value, previewing ? COL_SELECT : COL_TEXT, 0);

    bool on = s_model && menu_item_on(it, s, s_model);
    if (on != r->on) {
        r->on = on;
        lv_obj_set_style_bg_color(r->row, on ? COL_ROW_ON : COL_ROW, 0);
        if (r->sw) {
            if (on)
                lv_obj_add_state(r->sw, LV_STATE_CHECKED);
            else
                lv_obj_remove_state(r->sw, LV_STATE_CHECKED);
        }
    }
    draw_fader(i);

    int frame = i == s_selected ? FRAME_EDIT : i == s_highlight ? FRAME_HIGHLIGHT : FRAME_NONE;
    if (frame != r->frame) {
        r->frame = frame;
        lv_obj_set_style_border_width(r->row, frame == FRAME_EDIT ? 3 : frame ? 2 : 0, 0);
        lv_obj_set_style_border_color(r->row, frame == FRAME_EDIT ? COL_SELECT : COL_TEXT, 0);
    }
}

static void draw_title(void)
{
    const knob_slice_t *s = focus_slice();
    if (s)
        ui_set_textf(s_title, "SLICE %c  %s", s->letter, s->mode);
    else
        ui_set_text(s_title, "NO SLICE");
}

// ---------------------------------------------------------------- choices

static void send_pending(void)
{
    if (s_pending_row >= 0 && s_pending >= 0)
        post(ACT_MENU_CHOOSE, s_pending_row, s_pending);
    s_pending = -1;
    s_pending_row = -1;
}

static void preview(int row, int choice)
{
    if (s_pending_row >= 0 && s_pending_row != row)
        send_pending();
    s_pending_row = row;
    s_pending = choice;
    s_pending_ms = now_ms();
    draw_row(row);
}

// ---------------------------------------------------------------- navigation

static bool usable(int i)
{
    const menu_item_t *it = &g_menu_items[i];
    return it->kind != ITEM_HEADER && menu_item_visible(it, focus_slice());
}

static void set_highlight(int i)
{
    int old = s_highlight;
    s_highlight = i;
    if (old >= 0 && old != i)
        draw_row(old);
    if (i >= 0) {
        draw_row(i);
        lv_obj_scroll_to_view(s_rows[i].row, LV_ANIM_ON);
    }
}

// Leaves edit; a previewed choice is sent (confirm) or dropped (back).
static void leave_edit(bool confirm)
{
    if (s_selected < 0)
        return;
    int old = s_selected;
    if (confirm) {
        send_pending();
    } else if (s_pending_row == old) {
        s_pending = -1;
        s_pending_row = -1;
    }
    s_selected = -1;
    draw_row(old);
}

static void enter_edit(int i)
{
    s_selected = i;
    draw_row(i);
}

// A tap on a row: highlight it and put it on the dial, or release it when
// it is already there.
static void select_row(int i)
{
    bool was = s_selected == i;
    leave_edit(true);
    set_highlight(i);
    if (!was)
        enter_edit(i);
}

// Turning the dial with nothing in edit: move the highlight by `steps`
// usable rows, stopping at the ends. The list scrolls to keep it in view.
static void move_highlight(int steps)
{
    int i = s_highlight;
    int dir = steps > 0 ? 1 : -1;
    for (int n = steps > 0 ? steps : -steps; n > 0; --n) {
        int j = i + dir;
        while (j >= 0 && j < g_menu_item_count && !usable(j))
            j += dir;
        if (j < 0 || j >= g_menu_item_count)
            break;
        i = j;
    }
    if (i >= 0 && i != s_highlight)
        set_highlight(i);
}

static int first_usable(void)
{
    for (int i = 0; i < g_menu_item_count; ++i)
        if (usable(i))
            return i;
    return -1;
}

// ---------------------------------------------------------------- events

static void touched(int i)
{
    s_dial_ms = now_ms();   // touches keep the menu open like dial use does
    if (s_highlight != i) {
        leave_edit(true);
        set_highlight(i);
    }
}

static void row_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const menu_item_t *it = &g_menu_items[i];
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        if (it->kind == ITEM_TOGGLE)
            post(ACT_MENU_TOGGLE, i, 0);
        else
            select_row(i);
    } else if (code == LV_EVENT_LONG_PRESSED
               && (it->kind == ITEM_LEVEL_TOGGLE || it->kind == ITEM_LEVEL_CYCLE)) {
        post(ACT_MENU_TOGGLE, i, 0);
    }
}

// Faders: levels go to the radio as they move; a list choice previews while
// dragging and is sent when the finger lifts.
static void fader_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const menu_item_t *it = &g_menu_items[i];
    lv_obj_t *fader = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int value = lv_slider_get_value(fader);
    if (code == LV_EVENT_PRESSED) {
        touched(i);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        s_dial_ms = now_ms();
        if (is_choice(it)) {
            preview(i, value);
        } else {
            int step = it->step > 0 ? it->step : 1;
            int snapped = it->min + (value - it->min + step / 2) / step * step;
            post(ACT_MENU_SET_LEVEL, i, snapped);
        }
    } else if (code == LV_EVENT_RELEASED && is_choice(it) && s_pending_row == i) {
        send_pending();
        draw_row(i);
    } else if (code == LV_EVENT_PRESS_LOST) {
        draw_row(i);   // a scroll took over: show the radio's value again
    }
}

static void switch_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    touched(i);
    post(ACT_MENU_SET_SWITCH, i, lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void chip_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    touched(i);
    post(ACT_MENU_TOGGLE, i, 0);
}

static void menu_gesture(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_active());
        menu_close();
    }
}

static void grip_event(lv_event_t *e)
{
    (void)e;
    menu_close();
}

// ---------------------------------------------------------------- build

static void set_y(void *obj, int32_t y) { lv_obj_set_y(obj, y); }

static void hidden_when_done(lv_anim_t *a) { lv_obj_set_hidden(a->var, true); }

static void slide(int32_t from, int32_t to, bool hide_after)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_menu);
    lv_anim_set_exec_cb(&a, set_y);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (hide_after)
        lv_anim_set_completed_cb(&a, hidden_when_done);
    lv_anim_start(&a);
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

static void build_row(int i)
{
    const menu_item_t *it = &g_menu_items[i];
    row_t *r = &s_rows[i];
    void *idx = (void *)(intptr_t)i;
    r->row = lv_obj_create(s_list);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, lv_pct(100));
    lv_obj_set_scrollable(r->row, false);
    r->name = text(r->row, &lv_font_montserrat_20, COL_TEXT);
    lv_label_set_text(r->name, it->label);
    if (it->kind == ITEM_HEADER) {
        lv_obj_set_height(r->row, 30);
        lv_obj_set_style_text_font(r->name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(r->name, COL_HEAD, 0);
        lv_obj_align(r->name, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_clickable(r->row, false);
        return;
    }
    bool tall = has_fader(it);
    lv_obj_set_height(r->row, ROW_HEIGHT);
    lv_obj_set_style_radius(r->row, 10, 0);
    lv_obj_set_style_bg_color(r->row, COL_ROW, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r->row, 0, 0);
    lv_obj_set_style_pad_hor(r->row, 14, 0);
    lv_obj_add_event_cb(r->row, row_event, LV_EVENT_ALL, idx);
    lv_obj_align(r->name, tall ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 0, tall ? 9 : 0);

    // Right-hand side of the top line: [value] [chip] [switch].
    int right = 0;
    if (has_switch(it)) {
        r->sw = lv_switch_create(r->row);
        lv_obj_set_size(r->sw, 52, 26);
        lv_obj_set_style_bg_color(r->sw, COL_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(r->sw, COL_FILL, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_align(r->sw, tall ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_RIGHT_MID, 0, tall ? 7 : 0);
        lv_obj_add_event_cb(r->sw, switch_event, LV_EVENT_VALUE_CHANGED, idx);
        right = 62;
    }
    if (it->kind == ITEM_LEVEL_CYCLE) {
        r->chip = lv_button_create(r->row);
        lv_obj_set_size(r->chip, 62, 28);
        lv_obj_set_style_radius(r->chip, 14, 0);
        lv_obj_set_style_bg_color(r->chip, COL_TRACK, 0);
        lv_obj_set_style_shadow_width(r->chip, 0, 0);
        lv_obj_align(r->chip, LV_ALIGN_TOP_RIGHT, 0, 6);
        lv_obj_add_event_cb(r->chip, chip_event, LV_EVENT_SHORT_CLICKED, idx);
        r->chip_label = text(r->chip, &lv_font_montserrat_14, COL_TEXT);
        lv_obj_center(r->chip_label);
        right = 72;
    }
    r->value = text(r->row, &lv_font_montserrat_20, COL_TEXT);
    lv_obj_align(r->value, tall ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_RIGHT_MID, -right, tall ? 9 : 0);

    if (tall) {
        r->fader = lv_slider_create(r->row);
        lv_obj_set_size(r->fader, lv_pct(100), 8);
        lv_obj_align(r->fader, LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_obj_set_style_bg_color(r->fader, COL_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(r->fader, COL_FILL, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(r->fader, COL_TEXT, LV_PART_KNOB);
        lv_obj_set_style_pad_all(r->fader, 5, LV_PART_KNOB);
        lv_obj_set_style_opa(r->fader, LV_OPA_40, LV_STATE_DISABLED);
        // Signed values (RIT, filter edges) fill from zero, not from the left.
        if (it->min < 0 && !is_choice(it))
            lv_slider_set_mode(r->fader, LV_SLIDER_MODE_SYMMETRICAL);
        lv_slider_set_range(r->fader, is_choice(it) ? 0 : it->min, is_choice(it) ? 1 : it->max);
        r->fader_max = is_choice(it) ? 1 : it->max;
        lv_obj_add_event_cb(r->fader, fader_event, LV_EVENT_ALL, idx);
        set_enabled(r->fader, false);
    }
}

void menu_create(lv_obj_t *screen)
{
    s_menu = lv_obj_create(screen);
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, 480, 480);
    lv_obj_set_style_radius(s_menu, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_menu, COL_FACE, 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_menu, false);
    lv_obj_add_event_cb(s_menu, menu_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_set_hidden(s_menu, true);

    s_title = text(s_menu, &lv_font_montserrat_20, COL_HEAD);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 34);

    // The list fits the circle's widest band; rows scroll under the top and
    // bottom edges. A vertical drag that starts on a fader still scrolls it.
    s_list = lv_obj_create(s_menu);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 360, 334);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < g_menu_item_count; ++i)
        build_row(i);

    // Grip: tap it or swipe up from it (the list itself scrolls on a swipe).
    lv_obj_t *grip = lv_obj_create(s_menu);
    lv_obj_remove_style_all(grip);
    lv_obj_set_size(grip, 200, 56);
    lv_obj_align(grip, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(grip, grip_event, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *bar = lv_obj_create(grip);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 90, 7);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_bg_color(bar, COL_DIM, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_clickable(bar, false);
    lv_obj_center(bar);
}

// ---------------------------------------------------------------- control

bool menu_is_open(void) { return s_open; }

void menu_open(void)
{
    if (s_open)
        return;
    s_open = true;
    s_selected = -1;
    s_pending = -1;
    s_pending_row = -1;
    s_dial_ms = now_ms();
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    draw_title();
    s_highlight = first_usable();
    for (int i = 0; i < g_menu_item_count; ++i)
        draw_row(i);
    lv_obj_set_hidden(s_menu, false);
    lv_obj_move_foreground(s_menu);
    slide(-480, 0, false);
}

void menu_close(void)
{
    if (!s_open)
        return;
    leave_edit(true);
    send_pending();
    s_open = false;
    slide(lv_obj_get_y(s_menu), -480, true);
}

void menu_update(const knob_model_t *m, int focus, bool model_changed)
{
    s_model = m;
    s_focus = focus;
    if (!s_open)
        return;
    uint32_t now = now_ms();
    // A dial preview is sent once the dial rests (fader previews on release).
    if (s_pending >= 0 && s_pending_row == s_selected && now - s_pending_ms >= CHOICE_SEND_MS) {
        int row = s_pending_row;
        send_pending();
        draw_row(row);
    }
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if (now - s_dial_ms < idle)
        idle = now - s_dial_ms;
    if (idle >= IDLE_CLOSE_MS || m->link != LINK_BOUND) {
        menu_close();
        return;
    }
    if (model_changed) {
        draw_title();
        // A mode change can hide the rows in use (APF, CW settings).
        if (s_selected >= 0 && !usable(s_selected))
            leave_edit(false);
        if (s_highlight >= 0 && !usable(s_highlight))
            s_highlight = first_usable();
        for (int i = 0; i < g_menu_item_count; ++i)
            draw_row(i);
    }
}

void menu_dial_turn(int detents)
{
    s_dial_ms = now_ms();
    if (s_selected < 0) {
        move_highlight(detents);
        return;
    }
    const menu_item_t *it = &g_menu_items[s_selected];
    if (is_choice(it)) {
        int n = menu_choice_count(it, focus_slice());
        if (n <= 0)
            return;
        int cur = s_pending_row == s_selected && s_pending >= 0
                      ? s_pending
                      : menu_choice_current(it, focus_slice(), s_model);
        if (cur < 0)
            cur = 0;
        preview(s_selected, ((cur + detents) % n + n) % n);
    } else if (it->kind == ITEM_TOGGLE) {
        post(ACT_MENU_SET_SWITCH, s_selected, detents > 0);   // right = on, left = off
    } else {
        post(ACT_MENU_ADJUST, s_selected, detents);
    }
}

// Click: put the highlighted row in edit, or act on the row in edit.
void menu_dial_click(void)
{
    s_dial_ms = now_ms();
    if (s_selected < 0) {
        if (s_highlight >= 0)
            enter_edit(s_highlight);
        return;
    }
    switch (g_menu_items[s_selected].kind) {
    case ITEM_LEVEL_TOGGLE:   // flip the switch, keep the level on the dial
    case ITEM_LEVEL_CYCLE:    // step the mode, keep the level on the dial
        post(ACT_MENU_TOGGLE, s_selected, 0);
        break;
    default:                  // choices are sent now; levels are already live
        leave_edit(true);
        break;
    }
}

// Click-and-hold: back out. Leave edit (dropping a previewed choice), or
// close the menu when nothing is in edit.
void menu_dial_hold(void)
{
    s_dial_ms = now_ms();
    if (s_selected >= 0)
        leave_edit(false);
    else
        menu_close();
}

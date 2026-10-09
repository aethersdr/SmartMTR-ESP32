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
#define COL_TEXT    lv_color_hex(0xE8EEF2)
#define COL_DIM     lv_color_hex(0x6F8796)
#define COL_HEAD    lv_color_hex(0x37C8F5)
#define COL_SELECT  lv_color_hex(0xFFD60A)

#define ROW_HEIGHT      46
#define IDLE_CLOSE_MS   8000
#define CHOICE_SEND_MS  700    // a previewed choice is sent once the knob rests this long

typedef struct {
    lv_obj_t *row;
    lv_obj_t *name;
    lv_obj_t *value;
    bool on;            // last drawn state, so styles are only touched on change
    bool selected;
} row_t;

static row_t s_rows[MENU_ITEM_MAX];
static lv_obj_t *s_menu, *s_list, *s_title;
static bool s_open;
static int s_selected = -1;
static int s_pending = -1;           // previewed choice index, -1 when none
static uint32_t s_pending_ms;
static uint32_t s_knob_ms;
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

// ---------------------------------------------------------------- drawing

static void draw_row(int i)
{
    const menu_item_t *it = &g_menu_items[i];
    row_t *r = &s_rows[i];
    const knob_slice_t *s = focus_slice();
    lv_obj_set_hidden(r->row, !menu_item_visible(it, s));
    if (it->kind == ITEM_HEADER)
        return;

    char buf[40];
    bool previewing = i == s_selected && s_pending >= 0;
    if (previewing)
        menu_choice_label(it, s, s_pending, buf, sizeof buf);
    else if (s_model)
        menu_item_text(it, s, s_model, buf, sizeof buf);
    else
        buf[0] = '\0';
    ui_set_text(r->value, buf);
    lv_obj_set_style_text_color(r->value, previewing ? COL_SELECT : COL_TEXT, 0);

    bool on = s_model && menu_item_on(it, s, s_model);
    if (on != r->on) {
        r->on = on;
        lv_obj_set_style_bg_color(r->row, on ? COL_ROW_ON : COL_ROW, 0);
    }
    bool selected = i == s_selected;
    if (selected != r->selected) {
        r->selected = selected;
        lv_obj_set_style_border_width(r->row, selected ? 3 : 0, 0);
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
    if (s_selected >= 0 && s_pending >= 0)
        post(ACT_MENU_CHOOSE, s_selected, s_pending);
    s_pending = -1;
}

static void select_row(int i)
{
    int old = s_selected;
    send_pending();
    s_selected = (old == i) ? -1 : i;   // tapping the selected row releases it
    if (old >= 0)
        draw_row(old);
    if (s_selected >= 0)
        draw_row(s_selected);
}

// ---------------------------------------------------------------- events

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
    } else if (code == LV_EVENT_LONG_PRESSED && it->kind == ITEM_LEVEL_TOGGLE) {
        post(ACT_MENU_TOGGLE, i, 0);
    }
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

    s_title = lv_label_create(s_menu);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_title, COL_HEAD, 0);
    lv_label_set_text(s_title, "");
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 34);

    // The list fits the circle's widest band; rows scroll under the top and
    // bottom edges.
    s_list = lv_obj_create(s_menu);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, 360, 334);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < g_menu_item_count; ++i) {
        const menu_item_t *it = &g_menu_items[i];
        row_t *r = &s_rows[i];
        r->row = lv_obj_create(s_list);
        lv_obj_remove_style_all(r->row);
        lv_obj_set_width(r->row, lv_pct(100));
        lv_obj_set_scrollable(r->row, false);
        r->name = lv_label_create(r->row);
        lv_label_set_text(r->name, it->label);
        if (it->kind == ITEM_HEADER) {
            lv_obj_set_height(r->row, 30);
            lv_obj_set_style_text_font(r->name, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(r->name, COL_HEAD, 0);
            lv_obj_align(r->name, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_clickable(r->row, false);
            continue;
        }
        lv_obj_set_height(r->row, ROW_HEIGHT);
        lv_obj_set_style_radius(r->row, 10, 0);
        lv_obj_set_style_bg_color(r->row, COL_ROW, 0);
        lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(r->row, COL_SELECT, 0);
        lv_obj_set_style_pad_hor(r->row, 14, 0);
        lv_obj_add_event_cb(r->row, row_event, LV_EVENT_ALL, (void *)(intptr_t)i);
        lv_obj_set_style_text_font(r->name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(r->name, COL_TEXT, 0);
        lv_obj_align(r->name, LV_ALIGN_LEFT_MID, 0, 0);
        r->value = lv_label_create(r->row);
        lv_label_set_text(r->value, "");
        lv_obj_set_style_text_font(r->value, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(r->value, COL_TEXT, 0);
        lv_obj_align(r->value, LV_ALIGN_RIGHT_MID, 0, 0);
    }

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
    s_knob_ms = now_ms();
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    draw_title();
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
    send_pending();
    s_open = false;
    if (s_selected >= 0) {
        int old = s_selected;
        s_selected = -1;
        draw_row(old);
    }
    slide(lv_obj_get_y(s_menu), -480, true);
}

void menu_update(const knob_model_t *m, int focus, bool model_changed)
{
    s_model = m;
    s_focus = focus;
    if (!s_open)
        return;
    uint32_t now = now_ms();
    if (s_pending >= 0 && now - s_pending_ms >= CHOICE_SEND_MS) {
        int sel = s_selected;
        send_pending();
        draw_row(sel);
    }
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if (now - s_knob_ms < idle)
        idle = now - s_knob_ms;
    if (idle >= IDLE_CLOSE_MS || m->link != LINK_BOUND) {
        menu_close();
        return;
    }
    if (model_changed) {
        draw_title();
        for (int i = 0; i < g_menu_item_count; ++i)
            draw_row(i);
    }
}

void menu_knob_turn(int detents)
{
    s_knob_ms = now_ms();
    if (s_selected < 0) {
        lv_obj_scroll_by_bounded(s_list, 0, -detents * (ROW_HEIGHT + 6), LV_ANIM_ON);
        return;
    }
    const menu_item_t *it = &g_menu_items[s_selected];
    if (it->kind == ITEM_CHOICE || it->kind == ITEM_ONE_OF) {
        int n = menu_choice_count(it, focus_slice());
        if (n <= 0)
            return;
        int cur = s_pending >= 0 ? s_pending : menu_choice_current(it, focus_slice(), s_model);
        if (cur < 0)
            cur = 0;
        s_pending = ((cur + detents) % n + n) % n;
        s_pending_ms = s_knob_ms;
        draw_row(s_selected);
    } else {
        post(ACT_MENU_ADJUST, s_selected, detents);
    }
}

void menu_knob_press(void)
{
    s_knob_ms = now_ms();
    menu_close();
}

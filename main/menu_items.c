#include "menu_items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// NR variants: status keys, command keys, level keys. FLEX-8000/Aurora
// firmware has all five; on a 6000 the extended ones are no-ops.
static const char *const k_nr_status[] = {"nr", "nrl", "nrs", "rnn", "nrf", NULL};
static const char *const k_nr_set[]    = {"nr", "lms_nr", "speex_nr", "rnnoise", "nrf", NULL};
static const char *const k_nr_labels[] = {"NR", "NRL", "NRS", "RNN", "NRF", NULL};
static const char *const k_nr_levels[] = {"nr_level", "lms_nr_level", "speex_nr_level", NULL, "nrf_level"};

static const char *const k_anf_status[] = {"anf", "anfl", "anft", NULL};
static const char *const k_anf_set[]    = {"anf", "lms_anf", "anft", NULL};
static const char *const k_anf_labels[] = {"ANF", "ANFL", "ANFT", NULL};
static const char *const k_anf_levels[] = {"anf_level", "lms_anf_level", NULL};

static const char *const k_agc[] = {"off", "slow", "med", "fast", NULL};
static const char *const k_agc_labels[] = {"Off", "Slow", "Med", "Fast", NULL};

// Band ids for `display pan set ... band=` (AetherSDR XvtrPolicy): metres,
// 33 = WWV, 34 = general coverage.
static const char *const k_bands[] = {"160", "80", "60", "40", "30", "20", "17", "15",
                                      "12", "10", "6", "33", "34", NULL};
static const char *const k_band_labels[] = {"160m", "80m", "60m", "40m", "30m", "20m", "17m",
                                            "15m", "12m", "10m", "6m", "WWV", "GEN", NULL};

enum { ROW_NR_MODE = 9, ROW_ANF_MODE = 11 };

const menu_item_t g_menu_items[] = {
    {.label = "RECEIVE", .kind = ITEM_HEADER},
    {.label = "Mode", .kind = ITEM_CHOICE, .key = "mode", .list_key = "mode_list"},
    {.label = "RX antenna", .kind = ITEM_CHOICE, .key = "rxant", .list_key = "ant_list"},
    {.label = "Band", .kind = ITEM_CHOICE, .options = k_bands, .option_labels = k_band_labels,
     .special = SPECIAL_BAND},
    {.label = "RIT", .kind = ITEM_LEVEL_TOGGLE, .key = "rit_freq", .enable_key = "rit_on",
     .min = -9990, .max = 9990, .step = 10, .format = FMT_SIGNED_HZ},
    {.label = "Filter low", .kind = ITEM_LEVEL, .min = -12000, .max = 12000, .step = 50,
     .special = SPECIAL_FILTER_LO, .format = FMT_HZ, .modes = "!FM,NFM"},
    {.label = "Filter high", .kind = ITEM_LEVEL, .min = -12000, .max = 12000, .step = 50,
     .special = SPECIAL_FILTER_HI, .format = FMT_HZ, .modes = "!FM,NFM"},
    // AGC-T is the threshold while AGC runs and the fixed gain while it is
    // off, as SmartSDR and AetherSDR's AGC-T knob (#5384) treat it.
    {.label = "AGC", .kind = ITEM_LEVEL_CYCLE, .key = "agc_threshold", .cycle_key = "agc_mode",
     .first_level_key = "agc_off_level", .options = k_agc, .option_labels = k_agc_labels,
     .min = 0, .max = 100, .step = 1},
    {.label = "Squelch", .kind = ITEM_LEVEL_TOGGLE, .key = "squelch_level",
     .enable_key = "squelch", .min = 0, .max = 100, .step = 1},
    [ROW_NR_MODE] = {.label = "NR mode", .kind = ITEM_ONE_OF, .options = k_nr_status,
     .set_options = k_nr_set, .option_labels = k_nr_labels, .levels = k_nr_levels,
     .set_levels = k_nr_levels},
    {.label = "NR amount", .kind = ITEM_LEVEL, .min = 0, .max = 100, .step = 1,
     .special = SPECIAL_LEVEL_OF_ONE_OF, .one_of = ROW_NR_MODE},
    [ROW_ANF_MODE] = {.label = "ANF mode", .kind = ITEM_ONE_OF, .options = k_anf_status,
     .set_options = k_anf_set, .option_labels = k_anf_labels, .levels = k_anf_levels,
     .set_levels = k_anf_levels},
    {.label = "ANF amount", .kind = ITEM_LEVEL, .min = 0, .max = 100, .step = 1,
     .special = SPECIAL_LEVEL_OF_ONE_OF, .one_of = ROW_ANF_MODE},
    {.label = "WNB", .kind = ITEM_LEVEL_TOGGLE, .key = "wnb_level", .enable_key = "wnb",
     .min = 0, .max = 100, .step = 1},
    {.label = "APF", .kind = ITEM_LEVEL_TOGGLE, .key = "apf_level", .enable_key = "apf",
     .min = 0, .max = 100, .step = 1, .modes = "CW"},
    {.label = "Mute", .kind = ITEM_TOGGLE, .key = "audio_mute"},
    {.label = "Pan", .kind = ITEM_LEVEL, .key = "audio_pan", .min = 0, .max = 100, .step = 2,
     .format = FMT_PAN},
    {.label = "Diversity", .kind = ITEM_TOGGLE, .key = "diversity"},
    {.label = "DAX channel", .kind = ITEM_LEVEL, .key = "dax", .min = 0, .max = 8, .step = 1,
     .format = FMT_OFF_ZERO},
    {.label = "Tune lock", .kind = ITEM_TOGGLE, .key = "lock", .special = SPECIAL_LOCK},

    {.label = "TRANSMIT", .kind = ITEM_HEADER},
    {.label = "RF power", .scope = SCOPE_TX, .kind = ITEM_LEVEL, .key = "rfpower",
     .min = 0, .max = 100, .step = 1},
    {.label = "Tune power", .scope = SCOPE_TX, .kind = ITEM_LEVEL, .key = "tunepower",
     .min = 0, .max = 100, .step = 1},
    {.label = "Mic gain", .scope = SCOPE_TX, .kind = ITEM_LEVEL, .key = "mic_level",
     .set_key = "miclevel", .min = 0, .max = 100, .step = 1},
    {.label = "Compander", .scope = SCOPE_TX, .kind = ITEM_LEVEL_TOGGLE, .key = "compander_level",
     .enable_key = "compander", .min = 0, .max = 100, .step = 1},
    {.label = "Monitor", .scope = SCOPE_TX, .kind = ITEM_LEVEL_TOGGLE, .key = "mon_gain_sb",
     .enable_key = "sb_monitor", .set_enable_key = "mon", .min = 0, .max = 100, .step = 1},
    {.label = "TX antenna", .kind = ITEM_CHOICE, .key = "txant", .list_key = "tx_ant_list"},
    {.label = "XIT", .kind = ITEM_LEVEL_TOGGLE, .key = "xit_freq", .enable_key = "xit_on",
     .min = -9990, .max = 9990, .step = 10, .format = FMT_SIGNED_HZ},
    {.label = "CW speed", .scope = SCOPE_CW, .kind = ITEM_LEVEL, .key = "speed",
     .set_key = "wpm", .min = 5, .max = 100, .step = 1, .modes = "CW"},
    {.label = "CW pitch", .scope = SCOPE_CW, .kind = ITEM_LEVEL, .key = "pitch",
     .min = 100, .max = 6000, .step = 10, .format = FMT_HZ, .modes = "CW"},
    {.label = "Break-in", .scope = SCOPE_CW, .kind = ITEM_TOGGLE, .key = "break_in",
     .modes = "CW"},
};

const int g_menu_item_count = (int)(sizeof g_menu_items / sizeof g_menu_items[0]);
_Static_assert(sizeof g_menu_items / sizeof g_menu_items[0] <= MENU_ITEM_MAX,
               "raise MENU_ITEM_MAX for the menu screen");

// ---------------------------------------------------------------- lookups

static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static bool slice_scope(const menu_item_t *it) { return it->scope == SCOPE_SLICE; }

static const char *match_list(const char *const *list, const char *key)
{
    for (int i = 0; list && list[i]; ++i)
        if (streq(list[i], key))
            return list[i];
    return NULL;
}

const char *menu_tracked_key(bool slice_object, const char *key)
{
    for (int i = 0; i < g_menu_item_count; ++i) {
        const menu_item_t *it = &g_menu_items[i];
        if (it->kind == ITEM_HEADER || slice_scope(it) != slice_object)
            continue;
        if (streq(it->key, key))
            return it->key;
        if (streq(it->enable_key, key))
            return it->enable_key;
        if (streq(it->list_key, key))
            return it->list_key;
        if (streq(it->cycle_key, key))
            return it->cycle_key;
        if (streq(it->first_level_key, key))
            return it->first_level_key;
        if (it->kind == ITEM_ONE_OF) {
            const char *k = match_list(it->options, key);
            if (k)
                return k;
            for (int j = 0; it->options[j]; ++j)
                if (streq(it->levels[j], key))
                    return it->levels[j];
        }
    }
    return NULL;
}

static const knob_kv_t *kv_of(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m)
{
    return slice_scope(it) ? (s ? &s->kv : NULL) : &m->tx_kv;
}

int menu_item_with_special(item_special_t special)
{
    for (int i = 0; i < g_menu_item_count; ++i)
        if (g_menu_items[i].special == special)
            return i;
    return -1;
}

bool menu_item_visible(const menu_item_t *it, const knob_slice_t *s)
{
    if (it->kind == ITEM_HEADER || !it->modes)
        return true;
    if (!s)
        return false;
    bool exclude = it->modes[0] == '!';
    const char *list = it->modes + (exclude ? 1 : 0);
    bool listed = false;
    size_t n = strlen(s->mode);
    for (const char *p = list; *p;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == n && strncmp(p, s->mode, n) == 0)
            listed = true;
        p += len + (comma ? 1 : 0);
    }
    return exclude ? !listed : listed;
}

// Option of a ONE_OF row that is on, or -1.
static int one_of_active(const menu_item_t *it, const knob_kv_t *kv)
{
    for (int i = 0; kv && it->options[i]; ++i)
        if (kv_get_int(kv, it->options[i]))
            return i;
    return -1;
}

// Index of the LEVEL_CYCLE row's current option, or -1.
static int cycle_index(const menu_item_t *it, const knob_kv_t *kv)
{
    const char *cur = kv ? kv_get(kv, it->cycle_key) : "";
    for (int i = 0; it->options[i]; ++i)
        if (strcmp(it->options[i], cur) == 0)
            return i;
    return -1;
}

// The key a level row adjusts right now.
static const char *level_key(const menu_item_t *it, const knob_kv_t *kv)
{
    if (it->kind == ITEM_LEVEL_CYCLE && it->first_level_key && cycle_index(it, kv) == 0)
        return it->first_level_key;
    return it->key;
}

static int level_value(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m,
                       bool *known)
{
    *known = true;
    switch (it->special) {
    case SPECIAL_FILTER_LO:
        *known = s != NULL;
        return s ? s->filter_lo : 0;
    case SPECIAL_FILTER_HI:
        *known = s != NULL;
        return s ? s->filter_hi : 0;
    case SPECIAL_LEVEL_OF_ONE_OF: {
        const menu_item_t *owner = &g_menu_items[it->one_of];
        const knob_kv_t *kv = kv_of(owner, s, m);
        int on = one_of_active(owner, kv);
        if (on < 0 || !owner->levels[on]) {
            *known = false;
            return 0;
        }
        return kv_get_int(kv, owner->levels[on]);
    }
    default: {
        const knob_kv_t *kv = kv_of(it, s, m);
        const char *key = level_key(it, kv);
        *known = kv && kv_get(kv, key)[0];
        return kv ? kv_get_int(kv, key) : 0;
    }
    }
}

// ---------------------------------------------------------------- choices

// Copies item i of a comma list into buf.
static bool list_item(const char *list, int i, char *buf, size_t cap)
{
    for (const char *p = list; *p; --i) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (i == 0) {
            snprintf(buf, cap, "%.*s", (int)len, p);
            return true;
        }
        if (!comma)
            break;
        p = comma + 1;
    }
    return false;
}

static int list_count(const char *list)
{
    if (!list[0])
        return 0;
    int n = 1;
    for (const char *p = list; *p; ++p)
        n += *p == ',';
    return n - (list[strlen(list) - 1] == ',');  // tolerate a trailing comma
}

static int fixed_count(const char *const *list)
{
    int n = 0;
    while (list && list[n])
        ++n;
    return n;
}

int menu_choice_count(const menu_item_t *it, const knob_slice_t *s)
{
    if (it->kind == ITEM_ONE_OF)
        return fixed_count(it->options) + 1;  // + "Off"
    if (it->list_key)
        return s ? list_count(kv_get(&s->kv, it->list_key)) : 0;
    return fixed_count(it->options);
}

static void choice_value(const menu_item_t *it, const knob_slice_t *s, int i, char *buf, size_t cap)
{
    buf[0] = '\0';
    if (it->list_key) {
        if (s)
            list_item(kv_get(&s->kv, it->list_key), i, buf, cap);
    } else if (i >= 0 && i < fixed_count(it->options)) {
        snprintf(buf, cap, "%s", it->options[i]);
    }
}

void menu_choice_label(const menu_item_t *it, const knob_slice_t *s, int i, char *buf, size_t cap)
{
    if (it->kind == ITEM_ONE_OF) {
        snprintf(buf, cap, "%s", i == 0 ? "Off" : it->option_labels[i - 1]);
    } else if (it->option_labels && i >= 0 && i < fixed_count(it->option_labels)) {
        snprintf(buf, cap, "%s", it->option_labels[i]);
    } else {
        choice_value(it, s, i, buf, cap);
    }
}

// Amateur band of a frequency, as an index into k_bands; GEN when outside.
static int band_of(double mhz)
{
    static const struct { double lo, hi; } edges[] = {
        {1.8, 2.0}, {3.5, 4.0}, {5.25, 5.45}, {7.0, 7.3}, {10.1, 10.15}, {14.0, 14.35},
        {18.068, 18.168}, {21.0, 21.45}, {24.89, 24.99}, {28.0, 29.7}, {50.0, 54.0},
    };
    for (int i = 0; i < (int)(sizeof edges / sizeof edges[0]); ++i)
        if (mhz >= edges[i].lo && mhz <= edges[i].hi)
            return i;
    static const double wwv[] = {2.5, 5.0, 10.0, 15.0, 20.0};
    for (int i = 0; i < 5; ++i)
        if (fabs(mhz - wwv[i]) < 0.005)
            return 11;
    return 12;
}

int menu_choice_current(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m)
{
    if (it->special == SPECIAL_BAND)
        return s ? band_of(s->freq_mhz) : -1;
    if (it->kind == ITEM_ONE_OF)
        return one_of_active(it, kv_of(it, s, m)) + 1;
    const knob_kv_t *kv = kv_of(it, s, m);
    if (!kv)
        return -1;
    const char *cur = kv_get(kv, it->key);
    char buf[KV_VALUE_LEN];
    int n = menu_choice_count(it, s);
    for (int i = 0; i < n; ++i) {
        choice_value(it, s, i, buf, sizeof buf);
        if (strcasecmp(buf, cur) == 0)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------- display

bool menu_item_on(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m)
{
    const knob_kv_t *kv = kv_of(it, s, m);
    if (!kv)
        return false;
    switch (it->kind) {
    case ITEM_TOGGLE:
        return kv_get_int(kv, it->key) != 0;
    case ITEM_LEVEL_TOGGLE:
        return kv_get_int(kv, it->enable_key) != 0;
    case ITEM_ONE_OF:
        return one_of_active(it, kv) >= 0;
    case ITEM_LEVEL_CYCLE:
        return cycle_index(it, kv) > 0;
    default:
        return false;
    }
}

static void format_level(const menu_item_t *it, int v, char *buf, size_t cap)
{
    switch (it->format) {
    case FMT_SIGNED_HZ:
        snprintf(buf, cap, "%+d Hz", v);
        break;
    case FMT_HZ:
        snprintf(buf, cap, "%d Hz", v);
        break;
    case FMT_PAN:
        if (v == 50)
            snprintf(buf, cap, "C");
        else
            snprintf(buf, cap, "%c%d", v < 50 ? 'L' : 'R', abs(v - 50) * 2);
        break;
    case FMT_OFF_ZERO:
        if (v == 0)
            snprintf(buf, cap, "Off");
        else
            snprintf(buf, cap, "%d", v);
        break;
    default:
        snprintf(buf, cap, "%d", v);
        break;
    }
}

void menu_item_text(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m,
                    char *buf, size_t cap)
{
    buf[0] = '\0';
    switch (it->kind) {
    case ITEM_HEADER:
        return;
    case ITEM_TOGGLE: {
        const knob_kv_t *kv = kv_of(it, s, m);
        if (kv && kv_get(kv, it->key)[0])
            snprintf(buf, cap, "%s", menu_item_on(it, s, m) ? "On" : "Off");
        return;
    }
    case ITEM_CHOICE:
    case ITEM_ONE_OF: {
        int cur = menu_choice_current(it, s, m);
        if (cur >= 0)
            menu_choice_label(it, s, cur, buf, cap);
        return;
    }
    case ITEM_LEVEL:
    case ITEM_LEVEL_TOGGLE:
    case ITEM_LEVEL_CYCLE: {
        bool known;
        int v = level_value(it, s, m, &known);
        if (!known) {
            snprintf(buf, cap, "%s", it->special == SPECIAL_LEVEL_OF_ONE_OF ? "-" : "");
            return;
        }
        char level[24];
        format_level(it, v, level, sizeof level);
        if (it->kind == ITEM_LEVEL_TOGGLE) {
            snprintf(buf, cap, "%s  %s", menu_item_on(it, s, m) ? "On" : "Off", level);
        } else if (it->kind == ITEM_LEVEL_CYCLE) {
            int c = cycle_index(it, kv_of(it, s, m));
            snprintf(buf, cap, "%s  %s", c >= 0 ? it->option_labels[c] : "?", level);
        } else
            snprintf(buf, cap, "%s", level);
        return;
    }
    }
}

// ---------------------------------------------------------------- commands

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static const char *cmd_key(const char *set, const char *status) { return set ? set : status; }

// One `key=value` change in the row's scope.
static void set_cmd(const menu_item_t *it, int slice, const char *key, const char *value,
                    char *out)
{
    switch (it->scope) {
    case SCOPE_SLICE:
        snprintf(out, MENU_CMD_LEN, "slice set %d %s=%s", slice, key, value);
        break;
    case SCOPE_TX:
        snprintf(out, MENU_CMD_LEN, "transmit set %s=%s", key, value);
        break;
    case SCOPE_CW:
        snprintf(out, MENU_CMD_LEN, "cw %s %s", key, value);
        break;
    }
}

static knob_kv_t *kv_mut(const menu_item_t *it, knob_model_t *m, int slice)
{
    return slice_scope(it) ? &m->slices[slice].kv : &m->tx_kv;
}

// FlexLib Slice.UpdateFilter: per-mode passband limits, 50 Hz minimum width.
static bool clamp_filter(const knob_slice_t *s, int pitch, int *lo, int *hi)
{
    const char *md = s->mode;
    int lo_min = -12000, hi_max = 12000;
    if (!strcmp(md, "FM") || !strcmp(md, "NFM"))
        return false;
    if (!strcmp(md, "LSB") || !strcmp(md, "DIGL")) {
        hi_max = 0;
    } else if (!strcmp(md, "USB") || !strcmp(md, "DIGU") || !strcmp(md, "FDV")) {
        lo_min = 0;
    } else if (!strcmp(md, "CW")) {
        lo_min = -12000 - pitch;
        hi_max = 12000 - pitch;
    }
    *lo = clampi(*lo, lo_min, hi_max - 50);
    *hi = clampi(*hi, lo_min + 50, hi_max);
    return *hi - *lo >= 50;
}

int menu_adjust(int item, int slice, knob_model_t *m, int detents, menu_cmds_t out)
{
    const menu_item_t *it = &g_menu_items[item];
    knob_slice_t *s = &m->slices[slice];
    char v[16];
    if (it->special == SPECIAL_FILTER_LO || it->special == SPECIAL_FILTER_HI) {
        int lo = s->filter_lo, hi = s->filter_hi;
        // Moving one edge never pushes the other.
        if (it->special == SPECIAL_FILTER_LO) {
            lo += detents * it->step;
            if (lo > hi - 50)
                lo = hi - 50;
        } else {
            hi += detents * it->step;
            if (hi < lo + 50)
                hi = lo + 50;
        }
        if (!clamp_filter(s, kv_get_int(&m->tx_kv, "pitch"), &lo, &hi))
            return 0;
        s->filter_lo = lo;
        s->filter_hi = hi;
        snprintf(out[0], MENU_CMD_LEN, "filt %d %d %d", slice, lo, hi);
        return 1;
    }
    if (it->special == SPECIAL_LEVEL_OF_ONE_OF) {
        const menu_item_t *owner = &g_menu_items[it->one_of];
        knob_kv_t *kv = kv_mut(owner, m, slice);
        int on = one_of_active(owner, kv);
        if (on < 0 || !owner->levels[on])
            return 0;
        int nv = clampi(kv_get_int(kv, owner->levels[on]) + detents * it->step, it->min, it->max);
        snprintf(v, sizeof v, "%d", nv);
        kv_put(kv, owner->levels[on], v);
        set_cmd(owner, slice, owner->set_levels[on], v, out[0]);
        return 1;
    }
    if (it->kind != ITEM_LEVEL && it->kind != ITEM_LEVEL_TOGGLE && it->kind != ITEM_LEVEL_CYCLE)
        return 0;
    knob_kv_t *kv = kv_mut(it, m, slice);
    const char *key = level_key(it, kv);
    int nv = clampi(kv_get_int(kv, key) + detents * it->step, it->min, it->max);
    snprintf(v, sizeof v, "%d", nv);
    kv_put(kv, key, v);
    if (key == it->key && streq(key, "agc_threshold"))
        s->agc_threshold = nv;               // the dock's AGC tile reads this
    set_cmd(it, slice, key == it->key ? cmd_key(it->set_key, it->key) : key, v, out[0]);
    return 1;
}

int menu_toggle(int item, int slice, knob_model_t *m, menu_cmds_t out)
{
    const menu_item_t *it = &g_menu_items[item];
    knob_kv_t *kv = kv_mut(it, m, slice);
    const char *status, *command;
    if (it->kind == ITEM_TOGGLE) {
        status = it->key;
        command = cmd_key(it->set_key, it->key);
    } else if (it->kind == ITEM_LEVEL_TOGGLE) {
        status = it->enable_key;
        command = cmd_key(it->set_enable_key, it->enable_key);
    } else if (it->kind == ITEM_LEVEL_CYCLE) {
        // Next option, wrapping: off -> slow -> med -> fast -> off.
        int c = cycle_index(it, kv) + 1;    // unknown (-1) starts at options[0]
        if (!it->options[c])
            c = 0;
        const char *next = it->options[c];
        kv_put(kv, it->cycle_key, next);
        if (streq(it->cycle_key, "agc_mode") && slice_scope(it))
            snprintf(m->slices[slice].agc_mode, sizeof m->slices[slice].agc_mode, "%s", next);
        set_cmd(it, slice, it->cycle_key, next, out[0]);
        return 1;
    } else {
        return 0;
    }
    bool on = !kv_get_int(kv, status);
    kv_put(kv, status, on ? "1" : "0");
    if (it->special == SPECIAL_LOCK)
        snprintf(out[0], MENU_CMD_LEN, "slice %s %d", on ? "lock" : "unlock", slice);
    else
        set_cmd(it, slice, command, on ? "1" : "0", out[0]);
    return 1;
}

int menu_choose(int item, int slice, knob_model_t *m, int choice, menu_cmds_t out)
{
    const menu_item_t *it = &g_menu_items[item];
    knob_slice_t *s = &m->slices[slice];
    if (choice < 0 || choice >= menu_choice_count(it, s))
        return 0;
    if (it->kind == ITEM_ONE_OF) {
        // Turn the others off first, so two variants never run at once.
        knob_kv_t *kv = kv_mut(it, m, slice);
        int n = 0;
        for (int i = 0; it->options[i] && n < MENU_MAX_CMDS; ++i) {
            if (i != choice - 1 && kv_get_int(kv, it->options[i])) {
                kv_put(kv, it->options[i], "0");
                set_cmd(it, slice, it->set_options[i], "0", out[n++]);
            }
        }
        if (choice > 0 && n < MENU_MAX_CMDS) {
            kv_put(kv, it->options[choice - 1], "1");
            set_cmd(it, slice, it->set_options[choice - 1], "1", out[n++]);
        }
        return n;
    }
    char v[KV_VALUE_LEN];
    choice_value(it, s, choice, v, sizeof v);
    if (!v[0])
        return 0;
    if (it->special == SPECIAL_BAND) {
        if (!s->pan)
            return 0;
        snprintf(out[0], MENU_CMD_LEN, "display pan set 0x%08lX band=%s", (unsigned long)s->pan, v);
        return 1;
    }
    kv_put(kv_mut(it, m, slice), it->key, v);
    if (streq(it->key, "mode"))
        snprintf(s->mode, sizeof s->mode, "%.7s", v);
    if (streq(it->key, "rxant"))
        snprintf(s->rxant, sizeof s->rxant, "%.7s", v);
    set_cmd(it, slice, cmd_key(it->set_key, it->key), v, out[0]);
    return 1;
}

int menu_set_tx_slice(int slice, knob_model_t *m, menu_cmds_t out)
{
    for (int i = 0; i < KNOB_MAX_SLICES; ++i)
        m->slices[i].tx = i == slice;
    snprintf(out[0], MENU_CMD_LEN, "slice set %d tx=1", slice);
    return 1;
}

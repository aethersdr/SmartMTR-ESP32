// The swipe-down menu's items: one table row per adjustable radio value.
// flex_api.c stores every status key named here as it parses status, and
// builds the command for a change from the same row, so a new menu entry is
// one row in menu_items.c. Pure C; the host test covers it.
//
// Several values are commanded with one key and reported under another
// (`slice set N lms_nr=1` comes back as `nrl=1`), so each row names both.
// Wire facts: docs/smartsdr-api-notes.md.
#pragma once

#include "model.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SCOPE_SLICE,    // status `slice <N> key=`,  command `slice set <N> key=v`
    SCOPE_TX,       // status `transmit key=`,   command `transmit set key=v`
    SCOPE_CW,       // status `transmit key=`,   command `cw <key> <v>`
} item_scope_t;

typedef enum {
    ITEM_HEADER,        // section title, not adjustable
    ITEM_LEVEL,         // integer in [min, max]; the knob steps it
    ITEM_TOGGLE,        // 0/1; a tap flips it
    ITEM_LEVEL_TOGGLE,  // level on the knob, 0/1 enable on long-press
    ITEM_CHOICE,        // one value from a list; the knob previews, sent at rest
    ITEM_ONE_OF,        // which one of several 0/1 keys is on, or none ("Off")
    ITEM_LEVEL_CYCLE,   // level on the knob; long-press steps `cycle_key`
                        // through `options`. At options[0] the level is
                        // `first_level_key` instead of `key` (AGC off level).
} item_kind_t;

typedef enum {
    SPECIAL_NONE,
    SPECIAL_FILTER_LO,        // `filt <N> <lo> <hi>`
    SPECIAL_FILTER_HI,
    SPECIAL_BAND,             // `display pan set 0x<pan> band=<id>`
    SPECIAL_LOCK,             // `slice lock <N>` / `slice unlock <N>`
    SPECIAL_LEVEL_OF_ONE_OF,  // level of whichever option of `one_of` is on
} item_special_t;

typedef enum { FMT_PLAIN, FMT_SIGNED_HZ, FMT_HZ, FMT_PAN, FMT_OFF_ZERO } item_format_t;

typedef struct {
    const char *label;
    item_scope_t scope;
    item_kind_t kind;
    const char *key;              // status key
    const char *set_key;          // command key; NULL = same as key
    const char *enable_key;       // LEVEL_TOGGLE status key
    const char *cycle_key;        // LEVEL_CYCLE status and command key
    const char *first_level_key;  // LEVEL_CYCLE level key at options[0]
    const char *set_enable_key;   // NULL = same as enable_key
    int min, max, step;
    const char *list_key;         // CHOICE: status key holding a comma list
    const char *const *options;   // CHOICE fixed values / ONE_OF status keys
    const char *const *set_options;    // ONE_OF command keys
    const char *const *option_labels;  // display names, parallel to options
    const char *const *levels;    // ONE_OF: level status key per option (NULL: none)
    const char *const *set_levels;
    item_special_t special;
    int one_of;                   // LEVEL_OF_ONE_OF: index of the ONE_OF row
    item_format_t format;
    const char *modes;            // "CW" = only in these modes; "!FM,NFM" = all but
} menu_item_t;

#define MENU_ITEM_MAX 48   // rows the menu screen allocates

extern const menu_item_t g_menu_items[];
extern const int g_menu_item_count;

// The table's own string for a status key the menu tracks in `scope`'s
// object (slice, or transmit for TX and CW), or NULL. kv_put() stores it.
const char *menu_tracked_key(bool slice_object, const char *key);

// Whether a row applies to this slice's mode. Headers are always shown.
bool menu_item_visible(const menu_item_t *it, const knob_slice_t *s);

// Display text for a row's value ("" when unknown).
void menu_item_text(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m,
                    char *buf, size_t cap);
// Whether a toggle / level-toggle row is on (drives the row's highlight).
bool menu_item_on(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m);

// Choices for CHOICE / ONE_OF rows: count, the label of option i, and the
// index currently in effect (-1 when unknown).
int menu_choice_count(const menu_item_t *it, const knob_slice_t *s);
void menu_choice_label(const menu_item_t *it, const knob_slice_t *s, int i, char *buf, size_t cap);
int menu_choice_current(const menu_item_t *it, const knob_slice_t *s, const knob_model_t *m);

// Command builders. Each writes up to MENU_MAX_CMDS commands (without the
// `C<seq>|` prefix) into out, applies the new value to the model at once so
// the screen follows the knob, and returns how many commands it wrote.
#define MENU_MAX_CMDS 6
#define MENU_CMD_LEN  96
typedef char menu_cmds_t[MENU_MAX_CMDS][MENU_CMD_LEN];

int menu_adjust(int item, int slice, knob_model_t *m, int detents, menu_cmds_t out);
int menu_toggle(int item, int slice, knob_model_t *m, menu_cmds_t out);
int menu_choose(int item, int slice, knob_model_t *m, int choice, menu_cmds_t out);
int menu_set_tx_slice(int slice, knob_model_t *m, menu_cmds_t out);

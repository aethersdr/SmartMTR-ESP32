// Radio state the knob shows. The radio task writes it under model_lock();
// the UI copies it out under the same lock and never holds it while drawing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KNOB_MAX_SLICES 8
#define KNOB_NO_METER   (-1)

// Raw status values for the keys the swipe-down menu uses (menu_items.c).
// Keys point at the item table's own strings; keys ending in `_list` go to
// the longer list slots.
#define KV_MAX_ENTRIES  48
#define KV_VALUE_LEN    16
#define KV_MAX_LISTS    3
#define KV_LIST_LEN     112

typedef struct {
    const char *key[KV_MAX_ENTRIES];
    char value[KV_MAX_ENTRIES][KV_VALUE_LEN];
    const char *list_key[KV_MAX_LISTS];
    char list[KV_MAX_LISTS][KV_LIST_LEN];
} knob_kv_t;

void kv_put(knob_kv_t *kv, const char *key, const char *value);  // key: table string
const char *kv_get(const knob_kv_t *kv, const char *key);        // "" when unknown
int kv_get_int(const knob_kv_t *kv, const char *key);

typedef enum {
    LINK_NO_HOST,      // USB not enumerated, or AetherSDR's relay not started
    LINK_WAITING,      // answered HELLO, waiting for AetherSDR to reach the radio
    LINK_RADIO,        // radio connection open, no GUI client bound yet
    LINK_BOUND,        // bound to a GUI client's station
} link_state_t;

typedef struct {
    bool in_use;
    char letter;               // 'A'..'H'
    double freq_mhz;
    char mode[8];
    int filter_lo, filter_hi;  // Hz
    bool active;
    uint32_t client_handle;
    int audio_level;           // 0..100
    char agc_mode[8];
    int agc_threshold;         // 0..100
    bool nr, nb, anf;
    int nr_level, nb_level, anf_level;
    char rxant[8];
    int step;                  // Hz
    bool tx;
    int level_meter;           // meter id of this slice's S-meter, or KNOB_NO_METER
    float level_dbm;
    float level_peak_dbm;      // decaying peak for the extremes arc
    uint32_t pan;              // panadapter stream id, for band changes
    knob_kv_t kv;              // menu values
} knob_slice_t;

typedef struct {
    link_state_t link;
    uint32_t our_handle;
    uint32_t gui_handle;
    char gui_station[32];
    char gui_program[24];
    knob_slice_t slices[KNOB_MAX_SLICES];
    bool transmitting;
    int fwd_meter, swr_meter, mic_meter, comp_meter;
    float fwd_watts, swr, mic_db, comp_db;
    bool meters_seen;          // a VITA meter packet has arrived over the link
    knob_kv_t tx_kv;           // `transmit` values the menu uses
    // Audio spectrum from AetherSDR (link extension AUDIO_SPECTRUM): what the
    // operator hears, from AetherSDR's post-EQ RX tap. Levels 0..255 over
    // CTR2_SPECTRUM_FLOOR_DB..0 dBFS; bars split 0..spectrum_span_hz.
    uint8_t spectrum[64];
    int spectrum_bars;         // 0 until the first frame
    int spectrum_span_hz;
    uint32_t spectrum_ms;      // when it last arrived
    uint32_t revision;         // bumped on every change; the UI redraws on change
} knob_model_t;

void model_lock(void);
void model_unlock(void);
knob_model_t *model_get(void);  // caller holds the lock

// Index of the slice the dial drives: the bound client's active slice, else
// its lowest-numbered one; -1 when it has none.
int model_focus_slice(const knob_model_t *m);

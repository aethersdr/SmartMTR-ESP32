// Radio state the knob shows. The radio task writes it under model_lock();
// the UI copies it out under the same lock and never holds it while drawing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KNOB_MAX_SLICES 8
#define KNOB_NO_METER   (-1)

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
    uint32_t revision;         // bumped on every change; the UI redraws on change
} knob_model_t;

void model_lock(void);
void model_unlock(void);
knob_model_t *model_get(void);  // caller holds the lock

// Index of the slice the knob drives: the bound client's active slice, else
// its lowest-numbered one; -1 when it has none.
int model_focus_slice(const knob_model_t *m);

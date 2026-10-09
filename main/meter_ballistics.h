// SmartMTR meter ballistics, ported from AetherSDR's SmartMtrWidget so the
// knob's S-meters move exactly like the app's:
//  - the bar: MeterSmoother, an asymmetric exponential follower on the scale
//    fraction (attack 18.2 ms, release 269 ms; AetherSDR gui/MeterSmoother.h);
//  - the peak marker: MeterExtremes, the maximum over a sliding window of raw
//    dBm samples (3 s, AetherSDR's default Medium), approached at a constant
//    slew of 60 SmartMTR units/s and never below the bar
//    (AetherSDR gui/MeterExtremes.h, gui/SmartMtrStyle.h).
// Positions are scale fractions 0..1; SmartMTR's 10..230 UNIT span maps onto
// them, so the slew is 60 / 220 of the scale per second. Pure C, host-tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define METER_ATTACK_S        0.01818f
#define METER_RELEASE_S       0.26940f
#define METER_SNAP_EPS        0.0005f
#define METER_WINDOW_MS       3000u                 // SmartMtrExtremes::kWindowMediumSec
#define METER_SLEW_PER_S      (60.0f / 220.0f)      // kSlewUnitsPerSec over the UNIT span
#define METER_CONVERGE_EPS    (0.05f / 220.0f)      // MeterExtremes::kConvergeEps
#define METER_WINDOW_SAMPLES  128

typedef struct {
    float target;
    float display;
} meter_smoother_t;

void meter_smoother_set_target(meter_smoother_t *s, float target);
// Advances by elapsed wall-clock ms; true while still moving.
bool meter_smoother_tick(meter_smoother_t *s, uint32_t elapsed_ms);

typedef struct {
    uint32_t t[METER_WINDOW_SAMPLES];
    float raw[METER_WINDOW_SAMPLES];
    int head, count;
    float max_pos;      // peak marker position, scale fraction
    bool has_data;
} meter_extremes_t;

void meter_extremes_reset(meter_extremes_t *e);
// One raw sample (dBm) at a monotonic time in ms.
void meter_extremes_record(meter_extremes_t *e, float raw, uint32_t now_ms);
// Prunes the window, then slews the marker toward to_fraction(window max) at
// constant speed, clamped to stay at or above the bar (needle_pos).
void meter_extremes_tick(meter_extremes_t *e, uint32_t now_ms, uint32_t elapsed_ms,
                         float needle_pos, float (*to_fraction)(float raw));

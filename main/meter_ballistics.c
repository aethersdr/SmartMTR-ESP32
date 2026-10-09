#include "meter_ballistics.h"

#include <math.h>
#include <string.h>

void meter_smoother_set_target(meter_smoother_t *s, float target)
{
    s->target = target;
    if (fabsf(s->target - s->display) <= METER_SNAP_EPS)
        s->display = s->target;
}

bool meter_smoother_tick(meter_smoother_t *s, uint32_t elapsed_ms)
{
    const float delta = s->target - s->display;
    if (fabsf(delta) <= METER_SNAP_EPS) {
        s->display = s->target;
        return false;
    }
    if (elapsed_ms == 0)
        return true;
    const float tau = delta >= 0.0f ? METER_ATTACK_S : METER_RELEASE_S;
    const float alpha = 1.0f - expf(-(float)elapsed_ms / 1000.0f / tau);
    s->display += delta * alpha;
    return true;
}

void meter_extremes_reset(meter_extremes_t *e)
{
    memset(e, 0, sizeof *e);
}

void meter_extremes_record(meter_extremes_t *e, float raw, uint32_t now_ms)
{
    int i = (e->head + e->count) % METER_WINDOW_SAMPLES;
    if (e->count == METER_WINDOW_SAMPLES)
        e->head = (e->head + 1) % METER_WINDOW_SAMPLES;   // full: drop the oldest
    else
        e->count++;
    e->t[i] = now_ms;
    e->raw[i] = raw;
    e->has_data = true;
}

static float slew(float cur, float tgt, float step)
{
    const float d = tgt - cur;
    if (fabsf(d) <= METER_CONVERGE_EPS)
        return tgt;
    if (d > step)
        return cur + step;
    if (d < -step)
        return cur - step;
    return tgt;
}

void meter_extremes_tick(meter_extremes_t *e, uint32_t now_ms, uint32_t elapsed_ms,
                         float needle_pos, float (*to_fraction)(float raw))
{
    // 1) Prune samples older than the window; take the window maximum.
    while (e->count > 0 && now_ms - e->t[e->head] > METER_WINDOW_MS) {
        e->head = (e->head + 1) % METER_WINDOW_SAMPLES;
        e->count--;
    }
    e->has_data = e->count > 0;
    float max_raw = 0.0f;
    for (int k = 0; k < e->count; ++k) {
        const float r = e->raw[(e->head + k) % METER_WINDOW_SAMPLES];
        if (k == 0 || r > max_raw)
            max_raw = r;
    }
    // 2) Target: the mapped maximum, or the floor with no data.
    const float target = e->has_data ? to_fraction(max_raw) : 0.0f;
    // 3) Constant-velocity slew.
    const float step = METER_SLEW_PER_S * (float)elapsed_ms / 1000.0f;
    if (step > 0.0f)
        e->max_pos = slew(e->max_pos, target, step);
    // 4) Never below the bar, never below the floor.
    if (e->max_pos < needle_pos)
        e->max_pos = needle_pos;
    if (e->max_pos < 0.0f)
        e->max_pos = 0.0f;
}

#include "model.h"

#ifdef KNOB_HOST_TEST
void model_lock(void) {}
void model_unlock(void) {}
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;

void model_lock(void)
{
    if (!s_lock)
        s_lock = xSemaphoreCreateRecursiveMutex();  // first call is from app_main
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void model_unlock(void) { xSemaphoreGiveRecursive(s_lock); }
#endif

static knob_model_t s_model;

knob_model_t *model_get(void) { return &s_model; }

int model_focus_slice(const knob_model_t *m)
{
    if (m->link != LINK_BOUND)
        return -1;
    int first = -1;
    for (int i = 0; i < KNOB_MAX_SLICES; ++i) {
        const knob_slice_t *s = &m->slices[i];
        if (!s->in_use || s->client_handle != m->gui_handle)
            continue;
        if (s->active)
            return i;
        if (first < 0)
            first = i;
    }
    return first;
}

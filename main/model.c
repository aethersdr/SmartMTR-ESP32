#include "model.h"

#include <stdio.h>

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

#include <stdlib.h>
#include <string.h>

static knob_model_t s_model;

static bool is_list(const char *key)
{
    size_t n = strlen(key);
    return n > 5 && strcmp(key + n - 5, "_list") == 0;
}

void kv_put(knob_kv_t *kv, const char *key, const char *value)
{
    if (is_list(key)) {
        for (int i = 0; i < KV_MAX_LISTS; ++i) {
            if (!kv->list_key[i] || kv->list_key[i] == key) {
                kv->list_key[i] = key;
                snprintf(kv->list[i], KV_LIST_LEN, "%s", value);
                return;
            }
        }
        return;
    }
    for (int i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (!kv->key[i] || kv->key[i] == key) {
            kv->key[i] = key;
            snprintf(kv->value[i], KV_VALUE_LEN, "%s", value);
            return;
        }
    }
}

const char *kv_get(const knob_kv_t *kv, const char *key)
{
    if (!key)
        return "";
    for (int i = 0; i < KV_MAX_LISTS; ++i)
        if (kv->list_key[i] && strcmp(kv->list_key[i], key) == 0)
            return kv->list[i];
    for (int i = 0; i < KV_MAX_ENTRIES; ++i)
        if (kv->key[i] && strcmp(kv->key[i], key) == 0)
            return kv->value[i];
    return "";
}

int kv_get_int(const knob_kv_t *kv, const char *key) { return atoi(kv_get(kv, key)); }

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

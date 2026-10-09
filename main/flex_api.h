// SmartSDR TCP API client for a non-GUI controller. Transport-agnostic: the
// owner feeds it radio bytes and datagrams and supplies the two send
// functions. Everything here runs on one task (the radio task); it is not
// re-entrant. See docs/smartsdr-api-notes.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void (*flex_send_tcp_fn)(const char *bytes, size_t len);
typedef void (*flex_send_udp_fn)(uint16_t radio_port, const uint8_t *bytes, size_t len);

typedef enum {
    PARAM_AF,          // audio_level
    PARAM_AGC_T,       // agc_threshold
    PARAM_NR_LEVEL,
    PARAM_NB_LEVEL,
    PARAM_ANF_LEVEL,
    PARAM_FILTER_WIDTH,
    PARAM_COUNT
} knob_param_t;

typedef enum {
    ACT_TUNE,          // arg: encoder detents, signed
    ACT_STEP_NEXT,     // next entry in the slice's step_list
    ACT_ADJUST,        // param, arg: detents, signed
    ACT_TOGGLE_NR,
    ACT_TOGGLE_NB,
    ACT_TOGGLE_ANF,
    ACT_AGC_NEXT,      // off -> slow -> med -> fast -> off
} knob_action_kind_t;

typedef struct {
    knob_action_kind_t kind;
    knob_param_t param;
    int arg;
} knob_action_t;

void flex_api_init(flex_send_tcp_fn tcp, flex_send_udp_fn udp);

// The relay opened a fresh radio connection (host READY) / closed it.
void flex_api_connected(void);
void flex_api_disconnected(void);

// Radio TCP bytes, split at arbitrary points.
void flex_api_rx_bytes(const uint8_t *data, size_t len);
// One whole UDP datagram from the radio.
void flex_api_rx_datagram(uint16_t radio_port, const uint8_t *data, size_t len);

// Call about every 50 ms with a monotonic millisecond clock.
void flex_api_tick(uint32_t now_ms);

void flex_api_action(const knob_action_t *action);

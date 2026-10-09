#include "flex_api.h"

#include "menu_items.h"
#include "model.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM_NAME     "AetherKnob"
#define LINE_MAX_BYTES   8192   // `meter` definition lines run to several KB
#define MAX_METERS       192
#define PING_MS          1000
#define UDP_REGISTER_MS  250    // repeated until the first Flex VITA packet lands
#define TUNE_MIN_GAP_MS  40     // coalesce dial detents into one tune command
#define NO_CARRIER_WATTS 0.0011f

typedef enum { ROLE_NONE, ROLE_LEVEL, ROLE_FWD, ROLE_SWR, ROLE_MIC, ROLE_COMP } meter_role_t;

typedef struct {
    int id;
    char src[8];
    int num;
    char nam[12];
    char unit[8];
} meter_def_t;

static flex_send_tcp_fn s_tcp;
static flex_send_udp_fn s_udp;

static char s_line[LINE_MAX_BYTES];
static size_t s_line_len;
static bool s_line_overflow;

static bool s_connected;
static uint32_t s_seq;
static uint32_t s_last_ping_ms;
static uint32_t s_last_udp_reg_ms;
static bool s_sent_handshake;
static bool s_udp_confirmed;
static uint32_t s_now_ms;

static meter_def_t s_meters[MAX_METERS];
static int s_meter_count;

// Knob detents not yet sent as a tune command.
static int s_pending_detents;
static uint32_t s_last_tune_ms;

static char s_gui_client_id[48];

// ---------------------------------------------------------------- sending

static void send_cmd(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void send_cmd(const char *fmt, ...)
{
    if (!s_connected)
        return;
    char buf[256];
    int n = snprintf(buf, sizeof buf, "C%lu|", (unsigned long)s_seq++);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof buf - 1)
        n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    s_tcp(buf, (size_t)n);
}

// The radio cannot infer the relay's UDP endpoint, which is FlexLib's WAN
// case: register with an ASCII datagram naming our handle, repeated until
// a Flex VITA packet arrives (FlexLib Radio.cs RegisterUDP). `client udpport`
// would name a port on the PC, so it is never sent.
static void register_udp(uint32_t handle)
{
    char msg[48];
    int n = snprintf(msg, sizeof msg, "client udp_register handle=0x%lX", (unsigned long)handle);
    s_udp(4992, (const uint8_t *)msg, (size_t)n);
    s_last_udp_reg_ms = s_now_ms;
}

// ---------------------------------------------------------------- helpers

static uint32_t parse_handle(const char *s)
{
    return (uint32_t)strtoul(s, NULL, 16);  // accepts "0x" and bare hex
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src);
}

static bool streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void bump(knob_model_t *m) { m->revision++; }

// ---------------------------------------------------------------- meters

static meter_def_t *meter_find(int id, bool create)
{
    for (int i = 0; i < s_meter_count; ++i)
        if (s_meters[i].id == id)
            return &s_meters[i];
    if (!create || s_meter_count == MAX_METERS)
        return NULL;
    meter_def_t *d = &s_meters[s_meter_count++];
    memset(d, 0, sizeof *d);
    d->id = id;
    d->num = -1;
    return d;
}

static void meter_remove(int id)
{
    for (int i = 0; i < s_meter_count; ++i) {
        if (s_meters[i].id == id) {
            s_meters[i] = s_meters[--s_meter_count];
            return;
        }
    }
}

static bool is_tx_source(const meter_def_t *d) { return strncmp(d->src, "TX", 2) == 0; }

// Re-derives which meter id feeds which gauge after any definition change.
static void meter_resolve_roles(knob_model_t *m)
{
    m->fwd_meter = m->swr_meter = m->mic_meter = m->comp_meter = KNOB_NO_METER;
    for (int s = 0; s < KNOB_MAX_SLICES; ++s)
        m->slices[s].level_meter = KNOB_NO_METER;

    for (int i = 0; i < s_meter_count; ++i) {
        const meter_def_t *d = &s_meters[i];
        if (streq(d->src, "SLC") && streq(d->nam, "LEVEL")) {
            if (d->num >= 0 && d->num < KNOB_MAX_SLICES)
                m->slices[d->num].level_meter = d->id;
        } else if (is_tx_source(d) && streq(d->nam, "FWDPWR")) {
            m->fwd_meter = d->id;
        } else if (is_tx_source(d) && streq(d->nam, "SWR")) {
            m->swr_meter = d->id;
        } else if (streq(d->nam, "MICPEAK") && m->mic_meter == KNOB_NO_METER) {
            m->mic_meter = d->id;
        } else if (streq(d->nam, "COMPPEAK") && m->comp_meter == KNOB_NO_METER) {
            m->comp_meter = d->id;
        }
    }
}

static float meter_scale(const meter_def_t *d, int16_t raw)
{
    const char *u = d->unit;
    if (streq(u, "dB") || streq(u, "dBm") || streq(u, "dBFS") || streq(u, "SWR"))
        return raw / 128.0f;
    if (streq(u, "Volts") || streq(u, "Amps"))
        return raw / 256.0f;
    if (streq(u, "degC") || streq(u, "degF"))
        return raw / 64.0f;
    return (float)raw;
}

// "meter 12.src=SLC#12.num=0#12.nam=LEVEL#..." or "meter 12 removed".
static void handle_meter_status(knob_model_t *m, char *body)
{
    if (strstr(body, "removed")) {
        meter_remove(atoi(body));
        meter_resolve_roles(m);
        return;
    }
    for (char *rec = strtok(body, "#"); rec; rec = strtok(NULL, "#")) {
        char *dot = strchr(rec, '.');
        char *eq = dot ? strchr(dot, '=') : NULL;
        if (!dot || !eq)
            continue;
        *dot = '\0';
        *eq = '\0';
        meter_def_t *d = meter_find(atoi(rec), true);
        if (!d)
            continue;
        const char *key = dot + 1, *val = eq + 1;
        if (streq(key, "src"))
            copy_str(d->src, sizeof d->src, val);
        else if (streq(key, "num"))
            d->num = (int)strtol(val, NULL, 0);
        else if (streq(key, "nam"))
            copy_str(d->nam, sizeof d->nam, val);
        else if (streq(key, "unit"))
            copy_str(d->unit, sizeof d->unit, val);
    }
    meter_resolve_roles(m);
}

static void apply_meter(knob_model_t *m, int id, int16_t raw)
{
    const meter_def_t *d = meter_find(id, false);
    if (!d)
        return;
    float v = meter_scale(d, raw);
    if (id == m->fwd_meter) {
        float w = streq(d->unit, "dBm") ? powf(10.0f, v / 10.0f) / 1000.0f : v;
        m->fwd_watts = w <= NO_CARRIER_WATTS ? 0.0f : w;
    } else if (id == m->swr_meter) {
        m->swr = v;
    } else if (id == m->mic_meter) {
        m->mic_db = v;
    } else if (id == m->comp_meter) {
        m->comp_db = v;
    } else {
        for (int s = 0; s < KNOB_MAX_SLICES; ++s) {
            knob_slice_t *sl = &m->slices[s];
            if (sl->level_meter == id) {
                sl->level_dbm = v;
                if (v > sl->level_peak_dbm)
                    sl->level_peak_dbm = v;
            }
        }
    }
}

// ---------------------------------------------------------------- status

static void handle_slice_status(knob_model_t *m, char *body)
{
    char *save;
    char *idx = strtok_r(body, " ", &save);
    if (!idx)
        return;
    int n = atoi(idx);
    if (n < 0 || n >= KNOB_MAX_SLICES)
        return;
    knob_slice_t *s = &m->slices[n];
    if (!s->in_use) {
        int meter = s->level_meter;
        memset(s, 0, sizeof *s);
        s->level_meter = meter;
        s->letter = (char)('A' + n);
        s->level_dbm = s->level_peak_dbm = -127.0f;
    }
    s->in_use = true;

    for (char *kv = strtok_r(NULL, " ", &save); kv; kv = strtok_r(NULL, " ", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *k = kv, *v = eq + 1;
        if (streq(k, "in_use") && atoi(v) == 0) {
            s->in_use = false;
            break;
        } else if (streq(k, "RF_frequency")) s->freq_mhz = atof(v);
        else if (streq(k, "mode"))           copy_str(s->mode, sizeof s->mode, v);
        else if (streq(k, "filter_lo"))      s->filter_lo = atoi(v);
        else if (streq(k, "filter_hi"))      s->filter_hi = atoi(v);
        else if (streq(k, "active"))         s->active = atoi(v) != 0;
        else if (streq(k, "client_handle"))  s->client_handle = parse_handle(v);
        else if (streq(k, "audio_level"))    s->audio_level = atoi(v);
        else if (streq(k, "agc_mode"))       copy_str(s->agc_mode, sizeof s->agc_mode, v);
        else if (streq(k, "agc_threshold"))  s->agc_threshold = atoi(v);
        else if (streq(k, "nr"))             s->nr = atoi(v) != 0;
        else if (streq(k, "nb"))             s->nb = atoi(v) != 0;
        else if (streq(k, "anf"))            s->anf = atoi(v) != 0;
        else if (streq(k, "nr_level"))       s->nr_level = atoi(v);
        else if (streq(k, "nb_level"))       s->nb_level = atoi(v);
        else if (streq(k, "anf_level"))      s->anf_level = atoi(v);
        else if (streq(k, "rxant"))          copy_str(s->rxant, sizeof s->rxant, v);
        else if (streq(k, "step"))           s->step = atoi(v);
        else if (streq(k, "tx"))             s->tx = atoi(v) != 0;
        else if (streq(k, "index_letter"))   s->letter = v[0];
        else if (streq(k, "pan"))            s->pan = parse_handle(v);
        const char *tracked = menu_tracked_key(true, k);
        if (tracked)
            kv_put(&s->kv, tracked, v);
    }
}

static void bind_to(knob_model_t *m, uint32_t handle, const char *client_id,
                    const char *program, const char *station)
{
    m->gui_handle = handle;
    copy_str(s_gui_client_id, sizeof s_gui_client_id, client_id);
    copy_str(m->gui_program, sizeof m->gui_program, program);
    copy_str(m->gui_station, sizeof m->gui_station, station);
    for (char *p = m->gui_station; *p; ++p)
        if (*p == 0x7f)
            *p = ' ';
    m->link = LINK_BOUND;
    send_cmd("client bind client_id=%s", s_gui_client_id);
}

// "client 0x<h> connected client_id=<uuid> program=<p> station=<s> ..."
// "client 0x<h> disconnected ..."
static void handle_client_status(knob_model_t *m, char *body)
{
    char *save;
    char *h = strtok_r(body, " ", &save);
    char *what = strtok_r(NULL, " ", &save);
    if (!h || !what)
        return;
    uint32_t handle = parse_handle(h);

    if (streq(what, "disconnected")) {
        if (m->link == LINK_BOUND && handle == m->gui_handle) {
            m->link = LINK_RADIO;
            m->gui_handle = 0;
            s_gui_client_id[0] = '\0';
        }
        return;
    }
    if (!streq(what, "connected") || handle == m->our_handle)
        return;

    const char *client_id = "", *program = "", *station = "";
    for (char *kv = strtok_r(NULL, " ", &save); kv; kv = strtok_r(NULL, " ", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (streq(kv, "client_id"))    client_id = eq + 1;
        else if (streq(kv, "program")) program = eq + 1;
        else if (streq(kv, "station")) station = eq + 1;
    }
    // Only GUI clients carry a client_id. Prefer AetherSDR; otherwise take
    // the first GUI client seen.
    if (!client_id[0])
        return;
    bool unbound = m->link != LINK_BOUND;
    bool upgrade = m->link == LINK_BOUND && !streq(m->gui_program, "AetherSDR")
                   && streq(program, "AetherSDR");
    bool refresh = m->link == LINK_BOUND && handle == m->gui_handle;
    if (refresh) {
        copy_str(m->gui_station, sizeof m->gui_station, station);
        for (char *p = m->gui_station; *p; ++p)
            if (*p == 0x7f)
                *p = ' ';
    } else if (unbound || upgrade) {
        bind_to(m, handle, client_id, program, station);
    }
}

// "transmit key=val ..." (TX and CW settings). "transmit band ..." carries
// per-band settings, not the current ones, and is skipped.
static void handle_transmit_status(knob_model_t *m, char *body)
{
    if (strncmp(body, "band ", 5) == 0)
        return;
    char *save;
    for (char *kv = strtok_r(body, " ", &save); kv; kv = strtok_r(NULL, " ", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *tracked = menu_tracked_key(false, kv);
        if (tracked)
            kv_put(&m->tx_kv, tracked, eq + 1);
    }
}

static void handle_status(knob_model_t *m, char *payload)
{
    char *space = strchr(payload, ' ');
    if (!space)
        return;
    *space = '\0';
    char *body = space + 1;
    if (streq(payload, "slice"))
        handle_slice_status(m, body);
    else if (streq(payload, "client"))
        handle_client_status(m, body);
    else if (streq(payload, "meter"))
        handle_meter_status(m, body);
    else if (streq(payload, "transmit"))
        handle_transmit_status(m, body);
    else if (streq(payload, "interlock")) {
        char *st = strstr(body, "state=");
        if (st)
            m->transmitting = strncmp(st + 6, "TRANSMITTING", 12) == 0;
    }
    bump(m);
}

static void handle_line(char *line)
{
    knob_model_t *m = model_get();
    switch (line[0]) {
    case 'H':
        m->our_handle = parse_handle(line + 1);
        if (!s_sent_handshake) {
            s_sent_handshake = true;
            send_cmd("client program " PROGRAM_NAME);
            send_cmd("sub client all");
            send_cmd("sub slice all");
            send_cmd("sub tx all");
            send_cmd("sub meter all");
            send_cmd("keepalive enable");
            register_udp(m->our_handle);
        }
        bump(m);
        break;
    case 'S': {
        char *bar = strchr(line, '|');
        if (bar)
            handle_status(m, bar + 1);
        break;
    }
    default:  // V (version), R (replies), M (messages): nothing to act on
        break;
    }
}

// ---------------------------------------------------------------- public

void flex_api_init(flex_send_tcp_fn tcp, flex_send_udp_fn udp)
{
    s_tcp = tcp;
    s_udp = udp;
}

void flex_api_connected(void)
{
    model_lock();
    knob_model_t *m = model_get();
    memset(m, 0, sizeof *m);
    m->link = LINK_RADIO;
    meter_resolve_roles(m);
    bump(m);
    model_unlock();

    s_connected = true;
    s_seq = 1;
    s_line_len = 0;
    s_line_overflow = false;
    s_sent_handshake = false;
    s_udp_confirmed = false;
    s_meter_count = 0;
    s_pending_detents = 0;
    s_last_ping_ms = s_now_ms;
    s_gui_client_id[0] = '\0';
}

void flex_api_disconnected(void)
{
    s_connected = false;
    model_lock();
    knob_model_t *m = model_get();
    link_state_t keep = m->link == LINK_NO_HOST ? LINK_NO_HOST : LINK_WAITING;
    memset(m, 0, sizeof *m);
    m->link = keep;
    bump(m);
    model_unlock();
}

void flex_api_rx_bytes(const uint8_t *data, size_t len)
{
    model_lock();
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (!s_line_overflow && s_line_len > 0) {
                s_line[s_line_len] = '\0';
                handle_line(s_line);
            }
            s_line_len = 0;
            s_line_overflow = false;
        } else if (s_line_len < LINE_MAX_BYTES - 1) {
            s_line[s_line_len++] = c;
        } else {
            s_line_overflow = true;  // drop this line whole, never half of it
        }
    }
    model_unlock();
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void flex_api_rx_datagram(uint16_t radio_port, const uint8_t *d, size_t len)
{
    (void)radio_port;
    if (len < 4)
        return;
    uint32_t w0 = be32(d);
    unsigned type = w0 >> 28;
    bool has_class = (w0 >> 27) & 1;
    bool has_trailer = (w0 >> 26) & 1;
    unsigned tsi = (w0 >> 22) & 3, tsf = (w0 >> 20) & 3;
    size_t total = (size_t)(w0 & 0xFFFF) * 4;
    if (total > len || !has_class)
        return;

    size_t off = 4;
    if (type == 1 || type == 3)
        off += 4;                       // stream id
    if (off + 8 > total)
        return;
    uint32_t oui = be32(d + off) & 0xFFFFFF;
    uint16_t packet_class = be32(d + off + 4) & 0xFFFF;
    off += 8;
    if (tsi)
        off += 4;
    if (tsf)
        off += 8;
    size_t end = total - (has_trailer ? 4 : 0);
    if (oui != 0x001C2D)
        return;
    s_udp_confirmed = true;  // any Flex packet means the radio knows our endpoint
    if (packet_class != 0x8002 || off > end)
        return;

    model_lock();
    knob_model_t *m = model_get();
    m->meters_seen = true;
    for (; off + 4 <= end; off += 4) {
        int id = (d[off] << 8) | d[off + 1];
        int16_t raw = (int16_t)((d[off + 2] << 8) | d[off + 3]);
        apply_meter(m, id, raw);
    }
    bump(m);
    model_unlock();
}

static knob_slice_t *focus(knob_model_t *m, int *index)
{
    int i = model_focus_slice(m);
    if (index)
        *index = i;
    return i < 0 ? NULL : &m->slices[i];
}

static void flush_tune(knob_model_t *m)
{
    int n;
    knob_slice_t *s = focus(m, &n);
    // A locked slice refuses tuning; showing the detents would leave the
    // readout on a frequency the radio never took.
    if (!s || s_pending_detents == 0 || kv_get_int(&s->kv, "lock")) {
        s_pending_detents = 0;
        return;
    }
    int step = s->step > 0 ? s->step : 100;
    long hz = lround(s->freq_mhz * 1e6) + (long)s_pending_detents * step;
    hz -= hz % step;                    // land on the step grid, like a VFO knob
    if (hz < 0)
        hz = 0;
    s->freq_mhz = hz / 1e6;             // optimistic; the radio's status confirms
    s_pending_detents = 0;
    s_last_tune_ms = s_now_ms;
    send_cmd("slice tune %d %.6f", n, s->freq_mhz);
    bump(m);
}

void flex_api_tick(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (!s_connected)
        return;
    if (now_ms - s_last_ping_ms >= PING_MS) {
        s_last_ping_ms = now_ms;
        send_cmd("ping");
    }
    model_lock();
    knob_model_t *m = model_get();
    if (s_sent_handshake && !s_udp_confirmed && now_ms - s_last_udp_reg_ms >= UDP_REGISTER_MS)
        register_udp(m->our_handle);
    if (s_pending_detents && now_ms - s_last_tune_ms >= TUNE_MIN_GAP_MS)
        flush_tune(m);
    // Peak-hold decays 10 dB/s toward the live level.
    for (int i = 0; i < KNOB_MAX_SLICES; ++i) {
        knob_slice_t *s = &m->slices[i];
        if (s->level_peak_dbm > s->level_dbm)
            s->level_peak_dbm = fmaxf(s->level_dbm, s->level_peak_dbm - 0.5f);
    }
    model_unlock();
}

static void adjust(knob_model_t *m, int n, knob_slice_t *s, knob_param_t p, int detents)
{
    switch (p) {
    case PARAM_AF:
        s->audio_level = clampi(s->audio_level + detents, 0, 100);
        send_cmd("slice set %d audio_level=%d", n, s->audio_level);
        break;
    case PARAM_AGC_T:
        if (streq(s->agc_mode, "off")) {
            // AGC off: AGC-T is the fixed gain (agc_off_level), as in SmartSDR.
            const char *key = menu_tracked_key(true, "agc_off_level");
            int level = clampi(kv_get_int(&s->kv, "agc_off_level") + detents, 0, 100);
            char v[8];
            snprintf(v, sizeof v, "%d", level);
            kv_put(&s->kv, key, v);
            send_cmd("slice set %d agc_off_level=%d", n, level);
        } else {
            s->agc_threshold = clampi(s->agc_threshold + detents, 0, 100);
            char v[8];
            snprintf(v, sizeof v, "%d", s->agc_threshold);
            kv_put(&s->kv, menu_tracked_key(true, "agc_threshold"), v);
            send_cmd("slice set %d agc_threshold=%d", n, s->agc_threshold);
        }
        break;
    case PARAM_NR_LEVEL:
        s->nr_level = clampi(s->nr_level + detents, 0, 100);
        send_cmd("slice set %d nr_level=%d", n, s->nr_level);
        break;
    case PARAM_NB_LEVEL:
        s->nb_level = clampi(s->nb_level + detents, 0, 100);
        send_cmd("slice set %d nb_level=%d", n, s->nb_level);
        break;
    case PARAM_ANF_LEVEL:
        s->anf_level = clampi(s->anf_level + detents, 0, 100);
        send_cmd("slice set %d anf_level=%d", n, s->anf_level);
        break;
    case PARAM_FILTER_WIDTH: {
        // Widen/narrow symmetrically about the passband centre, 50 Hz/detent.
        int lo = s->filter_lo, hi = s->filter_hi;
        int width = clampi(hi - lo + detents * 50, 50, 12000);
        int centre = (lo + hi) / 2;
        lo = centre - width / 2;
        hi = lo + width;
        s->filter_lo = lo;
        s->filter_hi = hi;
        send_cmd("filt %d %d %d", n, lo, hi);
        break;
    }
    default:
        return;
    }
    bump(m);
}

static int next_step(int current)
{
    static const int steps[] = {10, 50, 100, 500, 1000, 5000, 10000};
    const int count = (int)(sizeof steps / sizeof steps[0]);
    for (int i = 0; i < count; ++i)
        if (steps[i] > current)
            return steps[i];
    return steps[0];
}

static bool valid_item(int item) { return item >= 0 && item < g_menu_item_count; }

// Menu and TX-slice actions: menu_items.c builds the commands and updates
// the model; this sends them.
static bool menu_action(knob_model_t *m, int n, const knob_action_t *a)
{
    menu_cmds_t cmds;
    int count;
    switch (a->kind) {
    case ACT_MENU_ADJUST:
        count = valid_item(a->item) ? menu_adjust(a->item, n, m, a->arg, cmds) : 0;
        break;
    case ACT_MENU_TOGGLE:
        count = valid_item(a->item) ? menu_toggle(a->item, n, m, cmds) : 0;
        break;
    case ACT_MENU_CHOOSE:
        count = valid_item(a->item) ? menu_choose(a->item, n, m, a->arg, cmds) : 0;
        break;
    case ACT_MENU_SET_LEVEL:
        count = valid_item(a->item) ? menu_set_level(a->item, n, m, a->arg, cmds) : 0;
        break;
    case ACT_MENU_SET_SWITCH:
        count = valid_item(a->item) ? menu_set_switch(a->item, n, m, a->arg != 0, cmds) : 0;
        break;
    case ACT_SET_TX: {
        int t = a->arg;
        bool ours = t >= 0 && t < KNOB_MAX_SLICES && m->slices[t].in_use
                    && m->slices[t].client_handle == m->gui_handle;
        count = ours ? menu_set_tx_slice(t, m, cmds) : 0;
        break;
    }
    default:
        return false;
    }
    for (int i = 0; i < count; ++i)
        send_cmd("%s", cmds[i]);
    bump(m);
    return true;
}

void flex_api_action(const knob_action_t *a)
{
    if (!s_connected)
        return;
    model_lock();
    knob_model_t *m = model_get();
    int n;
    knob_slice_t *s = focus(m, &n);
    if (!s || menu_action(m, n, a)) {
        model_unlock();
        return;
    }
    switch (a->kind) {
    case ACT_TUNE:
        s_pending_detents += a->arg;
        if (s_now_ms - s_last_tune_ms >= TUNE_MIN_GAP_MS)
            flush_tune(m);
        break;
    case ACT_STEP_NEXT:
        s->step = next_step(s->step);
        send_cmd("slice set %d step=%d", n, s->step);
        bump(m);
        break;
    case ACT_ADJUST:
        adjust(m, n, s, a->param, a->arg);
        break;
    case ACT_TOGGLE_NR:
        s->nr = !s->nr;
        send_cmd("slice set %d nr=%d", n, s->nr);
        bump(m);
        break;
    case ACT_TOGGLE_NB:
        s->nb = !s->nb;
        send_cmd("slice set %d nb=%d", n, s->nb);
        bump(m);
        break;
    case ACT_TOGGLE_ANF:
        s->anf = !s->anf;
        send_cmd("slice set %d anf=%d", n, s->anf);
        bump(m);
        break;
    case ACT_AGC_NEXT: {
        static const char *modes[] = {"off", "slow", "med", "fast"};
        int i = 0;
        while (i < 4 && !streq(modes[i], s->agc_mode))
            ++i;
        copy_str(s->agc_mode, sizeof s->agc_mode, modes[(i + 1) % 4]);
        kv_put(&s->kv, menu_tracked_key(true, "agc_mode"), s->agc_mode);
        send_cmd("slice set %d agc_mode=%s", n, s->agc_mode);
        bump(m);
        break;
    }
    default:
        break;
    }
    model_unlock();
}

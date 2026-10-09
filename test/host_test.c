// Host-side test of the SmartSDR client: plays a scripted radio session and
// checks the commands the knob sends back. Build: make -C test
#include "../main/flex_api.h"
#include "../main/model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static char sent[8192];
static size_t sent_len;
static int udp_count;
static uint16_t udp_port;

static void tcp(const char *b, size_t n)
{
    assert(sent_len + n < sizeof sent);
    memcpy(sent + sent_len, b, n);
    sent_len += n;
    sent[sent_len] = '\0';
}

static void udp(uint16_t port, const uint8_t *b, size_t n)
{
    (void)b;
    (void)n;
    udp_port = port;
    ++udp_count;
}

static void feed(const char *s) { flex_api_rx_bytes((const uint8_t *)s, strlen(s)); }

// Feed byte by byte too, since the relay splits radio output anywhere.
static void feed_split(const char *s)
{
    for (; *s; ++s)
        flex_api_rx_bytes((const uint8_t *)s, 1);
}

static void expect_sent(const char *needle)
{
    if (!strstr(sent, needle)) {
        fprintf(stderr, "missing command: %s\n--- sent ---\n%s", needle, sent);
        assert(0);
    }
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xFFFF); }

int main(void)
{
    flex_api_init(tcp, udp);
    flex_api_tick(1000);
    flex_api_connected();
    knob_model_t *m = model_get();
    assert(m->link == LINK_RADIO);

    feed("V1.4.0.0\nH5A0B1C2D\n");
    assert(m->our_handle == 0x5A0B1C2D);
    expect_sent("C1|client program AetherKnob\n");
    expect_sent("|sub client all\n");
    expect_sent("|sub slice all\n");
    expect_sent("|sub meter all\n");
    expect_sent("|keepalive enable\n");
    assert(!strstr(sent, "client gui"));
    assert(!strstr(sent, "udpport"));
    assert(udp_count == 1 && udp_port == 4992);

    // A non-GUI client (no client_id) must not be bound; a GUI client must.
    feed("S5A0B1C2D|client 0x11111111 connected program=SmartMTR station=Mac\n");
    assert(m->link == LINK_RADIO);
    feed_split("S5A0B1C2D|client 0x22222222 connected client_id=AB-CD program=SmartSDR-Win "
               "station=Shack\x7fPC local_ptt=1\n");
    assert(m->link == LINK_BOUND && m->gui_handle == 0x22222222);
    assert(strcmp(m->gui_station, "Shack PC") == 0);
    expect_sent("|client bind client_id=AB-CD\n");
    // AetherSDR is preferred over another GUI client.
    feed("S5A0B1C2D|client 0x33333333 connected client_id=EF-01 program=AetherSDR station=Arch\n");
    assert(m->gui_handle == 0x33333333);
    expect_sent("|client bind client_id=EF-01\n");

    // Slices: slice 0 belongs to someone else; 1 and 2 are AetherSDR's, 2 active.
    feed("S5A0B1C2D|slice 0 in_use=1 RF_frequency=7.074000 client_handle=0x22222222 active=1\n");
    feed("S5A0B1C2D|slice 1 in_use=1 RF_frequency=14.074000 mode=DIGU client_handle=0x33333333 active=0\n");
    feed("S5A0B1C2D|slice 2 in_use=1 RF_frequency=14.172500 mode=USB filter_lo=100 filter_hi=2800 "
         "client_handle=0x33333333 active=1 step=100 audio_level=50 agc_mode=med agc_threshold=60 "
         "nr=0 nb=1 anf=0 nr_level=40 rxant=ANT2\n");
    assert(model_focus_slice(m) == 2);
    assert(fabs(m->slices[2].freq_mhz - 14.1725) < 1e-9);
    assert(strcmp(m->slices[2].rxant, "ANT2") == 0 && m->slices[2].nb);

    // Tuning: 3 detents of 100 Hz -> one coalesced tune command.
    sent_len = 0;
    sent[0] = '\0';
    flex_api_tick(2000);
    sent_len = 0;
    knob_action_t a = {.kind = ACT_TUNE, .arg = 3};
    flex_api_action(&a);
    expect_sent("|slice tune 2 14.172800\n");
    a.arg = -1;
    flex_api_action(&a);          // within the coalescing window: held
    a.arg = -1;
    flex_api_action(&a);
    flex_api_tick(2100);
    expect_sent("|slice tune 2 14.172600\n");

    knob_action_t step = {.kind = ACT_STEP_NEXT};
    flex_api_action(&step);
    expect_sent("|slice set 2 step=500\n");

    knob_action_t af = {.kind = ACT_ADJUST, .param = PARAM_AF, .arg = -5};
    flex_api_action(&af);
    expect_sent("|slice set 2 audio_level=45\n");
    knob_action_t width = {.kind = ACT_ADJUST, .param = PARAM_FILTER_WIDTH, .arg = 2};
    flex_api_action(&width);
    expect_sent("|filt 2 50 2850\n");
    knob_action_t nr = {.kind = ACT_TOGGLE_NR};
    flex_api_action(&nr);
    expect_sent("|slice set 2 nr=1\n");
    knob_action_t agc = {.kind = ACT_AGC_NEXT};
    flex_api_action(&agc);
    expect_sent("|slice set 2 agc_mode=fast\n");

    // Meters: definitions, then a VITA-49 meter packet.
    feed("S5A0B1C2D|meter 7.src=SLC#7.num=2#7.nam=LEVEL#7.unit=dBm#7.low=-150.0#7.hi=20.0#"
         "9.src=TX-#9.num=1#9.nam=FWDPWR#9.unit=dBm#10.src=TX-#10.num=1#10.nam=SWR#10.unit=SWR#\n");
    assert(m->slices[2].level_meter == 7 && m->fwd_meter == 9 && m->swr_meter == 10);

    uint8_t pkt[64] = {0};
    size_t words = 7 + 3;                         // 28-byte header + 3 meter pairs
    put32(pkt, (3u << 28) | (1u << 27) | (1u << 22) | (1u << 20) | words);
    put32(pkt + 4, 0x00000700);                   // stream id
    put32(pkt + 8, 0x001C2D);                     // OUI
    put32(pkt + 12, 0x534C8002);                  // info class, packet class 0x8002
    put16(pkt + 28, 7);  put16(pkt + 30, (uint16_t)(int16_t)(-73 * 128));
    put16(pkt + 32, 9);  put16(pkt + 34, (uint16_t)(int16_t)(50 * 128));    // 50 dBm = 100 W
    put16(pkt + 36, 10); put16(pkt + 38, (uint16_t)(int16_t)(1.5 * 128));
    flex_api_rx_datagram(4992, pkt, words * 4);
    assert(m->meters_seen);
    assert(fabsf(m->slices[2].level_dbm + 73.0f) < 0.01f);
    assert(fabsf(m->fwd_watts - 100.0f) < 0.1f);
    assert(fabsf(m->swr - 1.5f) < 0.01f);

    // A meter removal unhooks the gauge.
    feed("S5A0B1C2D|meter 10 removed\n");
    assert(m->swr_meter == KNOB_NO_METER);

    // Slice removal and GUI client departure.
    feed("S5A0B1C2D|slice 2 in_use=0\n");
    assert(model_focus_slice(m) == 1);
    feed("S5A0B1C2D|client 0x33333333 disconnected forced=0\n");
    assert(m->link == LINK_RADIO && model_focus_slice(m) == -1);

    // An over-long line is dropped whole and the stream recovers.
    static char big[9000];
    memset(big, 'x', sizeof big - 2);
    big[0] = 'S';
    big[sizeof big - 2] = '\n';
    feed(big);
    feed("S5A0B1C2D|client 0x44444444 connected client_id=99 program=AetherSDR station=B\n");
    assert(m->link == LINK_BOUND && m->gui_handle == 0x44444444);

    flex_api_disconnected();
    assert(m->link == LINK_WAITING);
    puts("host_test: all checks passed");
    return 0;
}

// Host-side test of the SmartSDR client: plays a scripted radio session and
// checks the commands the knob sends back. Build: make -C test
#include "../main/flex_api.h"
#include "../main/menu_items.h"
#include "../main/meter_ballistics.h"
#include "../main/model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static char sent[8192];
static size_t sent_len;
static int udp_count;
static uint16_t udp_port;
static char udp_last[64];

static void tcp(const char *b, size_t n)
{
    assert(sent_len + n < sizeof sent);
    memcpy(sent + sent_len, b, n);
    sent_len += n;
    sent[sent_len] = '\0';
}

static void udp(uint16_t port, const uint8_t *b, size_t n)
{
    snprintf(udp_last, sizeof udp_last, "%.*s", (int)n, (const char *)b);
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

static int item(const char *label)
{
    for (int i = 0; i < g_menu_item_count; ++i)
        if (strcmp(g_menu_items[i].label, label) == 0)
            return i;
    fprintf(stderr, "no menu item %s\n", label);
    assert(0);
    return -1;
}

static void act(knob_action_kind_t kind, const char *label, int arg)
{
    knob_action_t a = {.kind = kind, .item = label ? item(label) : 0, .arg = arg};
    flex_api_action(&a);
}

static void expect_text(const char *label, const char *want)
{
    knob_model_t *m = model_get();
    char buf[48];
    int f = model_focus_slice(m);
    menu_item_text(&g_menu_items[item(label)], &m->slices[f], m, buf, sizeof buf);
    if (strcmp(buf, want) != 0) {
        fprintf(stderr, "%s shows '%s', want '%s'\n", label, buf, want);
        assert(0);
    }
}

static void test_menu(void)
{
    knob_model_t *m = model_get();
    feed("S5A0B1C2D|slice 2 mode_list=LSB,USB,CW,DIGU ant_list=ANT1,ANT2,RX_A rit_on=0 rit_freq=0 "
         "nrl=1 lms_nr_level=30 squelch=1 squelch_level=20 audio_pan=50 lock=0 pan=0x40000000 dax=0\n");
    feed("S5A0B1C2D|transmit rfpower=50 tunepower=10 mic_level=40 compander=1 compander_level=60 "
         "sb_monitor=0 mon_gain_sb=50 speed=25 pitch=600 break_in=0\n");
    feed("S5A0B1C2D|transmit band 20 rfpower=99\n");  // per-band, not current
    assert(m->slices[2].pan == 0x40000000);
    expect_text("RF power", "50");
    expect_text("NR mode", "NRL");
    expect_text("NR amount", "30");
    expect_text("Squelch", "On  20");
    expect_text("Pan", "C");
    expect_text("RX antenna", "ANT2");
    expect_text("DAX channel", "Off");
    expect_text("Band", "20m");

    sent_len = 0;
    act(ACT_MENU_ADJUST, "NR amount", 5);
    expect_sent("|slice set 2 lms_nr_level=35\n");      // command key, not status key
    act(ACT_MENU_CHOOSE, "NR mode", 3);                  // NRS
    expect_sent("|slice set 2 lms_nr=0\n");
    expect_sent("|slice set 2 speex_nr=1\n");
    expect_text("NR mode", "NRS");
    act(ACT_MENU_CHOOSE, "NR mode", 0);                  // Off
    expect_sent("|slice set 2 speex_nr=0\n");
    expect_text("NR amount", "-");

    // AGC: knob sets the threshold while AGC runs, the off level while off;
    // long-press steps off -> slow -> med -> fast -> off.
    feed("S5A0B1C2D|slice 2 agc_mode=med agc_threshold=65 agc_off_level=40\n");
    expect_text("AGC", "Med  65");
    act(ACT_MENU_ADJUST, "AGC", 2);
    expect_sent("|slice set 2 agc_threshold=67\n");
    act(ACT_MENU_TOGGLE, "AGC", 0);
    expect_sent("|slice set 2 agc_mode=fast\n");
    act(ACT_MENU_TOGGLE, "AGC", 0);
    expect_sent("|slice set 2 agc_mode=off\n");
    assert(strcmp(m->slices[2].agc_mode, "off") == 0);
    expect_text("AGC", "Off  40");
    act(ACT_MENU_ADJUST, "AGC", -5);
    expect_sent("|slice set 2 agc_off_level=35\n");
    knob_action_t tile = {.kind = ACT_ADJUST, .param = PARAM_AGC_T, .arg = 1};
    flex_api_action(&tile);                              // dock tile follows the mode
    expect_sent("|slice set 2 agc_off_level=36\n");
    act(ACT_MENU_TOGGLE, "AGC", 0);
    expect_sent("|slice set 2 agc_mode=slow\n");
    tile.arg = -2;
    flex_api_action(&tile);
    expect_sent("|slice set 2 agc_threshold=65\n");
    expect_text("AGC", "Slow  65");

    // Faders and switches send absolute values, clamped to the row's range.
    act(ACT_MENU_SET_LEVEL, "RF power", 37);
    expect_sent("|transmit set rfpower=37\n");
    act(ACT_MENU_SET_LEVEL, "RF power", 150);
    expect_sent("|transmit set rfpower=100\n");
    act(ACT_MENU_SET_SWITCH, "Mute", 1);
    expect_sent("|slice set 2 audio_mute=1\n");
    act(ACT_MENU_SET_SWITCH, "Squelch", 0);
    expect_sent("|slice set 2 squelch=0\n");
    expect_text("Squelch", "Off  20");
    act(ACT_MENU_SET_LEVEL, "Filter high", 2400);
    expect_sent("|filt 2 50 2400\n");

    act(ACT_MENU_ADJUST, "Mic gain", 1);
    expect_sent("|transmit set miclevel=41\n");
    act(ACT_MENU_TOGGLE, "Monitor", 0);
    expect_sent("|transmit set mon=1\n");
    act(ACT_MENU_TOGGLE, "Compander", 0);
    expect_sent("|transmit set compander=0\n");

    act(ACT_MENU_ADJUST, "RIT", 3);
    expect_sent("|slice set 2 rit_freq=30\n");
    act(ACT_MENU_TOGGLE, "RIT", 0);
    expect_sent("|slice set 2 rit_on=1\n");
    expect_text("RIT", "On  +30 Hz");

    act(ACT_MENU_ADJUST, "Pan", -5);
    expect_sent("|slice set 2 audio_pan=40\n");
    expect_text("Pan", "L20");

    // USB passband cannot go below 0 Hz (FlexLib UpdateFilter).
    act(ACT_MENU_ADJUST, "Filter low", -20);
    expect_sent("|filt 2 0 2400\n");
    act(ACT_MENU_ADJUST, "Filter high", -100);         // cannot cross the low edge
    expect_sent("|filt 2 0 50\n");

    act(ACT_MENU_TOGGLE, "Tune lock", 0);
    expect_sent("|slice lock 2\n");
    // While locked, turning the dial sends nothing and the readout stays put.
    double locked_at = m->slices[2].freq_mhz;
    sent_len = 0;
    sent[0] = '\0';
    knob_action_t turn = {.kind = ACT_TUNE, .arg = 5};
    flex_api_action(&turn);
    flex_api_tick(2150);
    assert(!strstr(sent, "slice tune") && m->slices[2].freq_mhz == locked_at);
    act(ACT_MENU_TOGGLE, "Tune lock", 0);
    expect_sent("|slice unlock 2\n");
    act(ACT_MENU_CHOOSE, "Band", 3);
    expect_sent("|display pan set 0x40000000 band=40\n");

    // CW-only rows appear with CW, and use `cw` commands.
    const menu_item_t *speed = &g_menu_items[item("CW speed")];
    assert(!menu_item_visible(speed, &m->slices[2]));
    act(ACT_MENU_CHOOSE, "Mode", 2);
    expect_sent("|slice set 2 mode=CW\n");
    assert(menu_item_visible(speed, &m->slices[2]));
    assert(!menu_item_visible(&g_menu_items[item("Filter low")], &(knob_slice_t){.mode = "FM"}));
    act(ACT_MENU_ADJUST, "CW speed", 1);
    expect_sent("|cw wpm 26\n");

    act(ACT_SET_TX, NULL, 1);
    expect_sent("|slice set 1 tx=1\n");
    assert(m->slices[1].tx && !m->slices[2].tx);
    act(ACT_SET_TX, NULL, 0);                            // not this station's slice
    assert(!strstr(sent, "slice set 0 tx=1"));
}

// S-meter mapping AetherSDR's SmartMTR uses: S0..S9 lower half, S9..+60 upper.
static float frac(float dbm)
{
    if (dbm <= -127.0f) return 0.0f;
    if (dbm <= -73.0f) return 0.5f * (dbm + 127.0f) / 54.0f;
    if (dbm >= -13.0f) return 1.0f;
    return 0.5f + 0.5f * (dbm + 73.0f) / 60.0f;
}

// The port matches AetherSDR's SmartMtrWidget: MeterSmoother time constants,
// MeterExtremes' 3 s window and 60-unit/s slew, and the marker >= bar clamp.
static void test_meter_ballistics(void)
{
    meter_smoother_t bar = {0};
    meter_smoother_set_target(&bar, 1.0f);
    meter_smoother_tick(&bar, 18);                      // one attack time constant (18.2 ms)
    assert(fabsf(bar.display - (1.0f - expf(-18.0f / 18.18f))) < 1e-4f);
    bar.display = 1.0f;
    meter_smoother_set_target(&bar, 0.0f);
    meter_smoother_tick(&bar, 269);                     // one release time constant (269.4 ms)
    assert(fabsf(bar.display - expf(-269.0f / 269.4f)) < 1e-3f);

    // S9 for a second, then nothing: the bar sags, the marker holds the window
    // maximum for 3 s after the last S9 sample, then glides at 60/220 per second.
    meter_smoother_t b = {0};
    meter_extremes_t e;
    meter_extremes_reset(&e);
    uint32_t t = 0;
    float level = -73.0f;
    for (; t <= 5500; t += 33) {
        if (t >= 1000)
            level = -127.0f;
        meter_smoother_set_target(&b, frac(level));
        meter_extremes_record(&e, level, t);
        meter_smoother_tick(&b, 33);
        meter_extremes_tick(&e, t, 33, b.display, frac);
        assert(e.max_pos >= b.display - 1e-6f);         // never below the bar
        if (t == 990)
            assert(fabsf(e.max_pos - 0.5f) < 1e-3f);    // the bar pushed it straight up to S9
        if (t >= 1500 && t <= 3960)
            assert(fabsf(e.max_pos - 0.5f) < 1e-3f);    // held: S9 still in the 3 s window
        if (t >= 1500 && t <= 3960)
            assert(b.display < 0.2f);                   // while the bar has sagged
    }
    // Last S9 sample at t = 990, window expired after t = 3990; ~1.5 s of
    // glide at 60/220 per second has taken it down by about 0.41.
    assert(e.max_pos < 0.5f - 0.35f && e.max_pos > 0.0f);
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xFFFF); }

int main(void)
{
    test_meter_ballistics();
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
    assert(strcmp(udp_last, "client udp_register handle=0x5A0B1C2D") == 0);

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
    test_menu();

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
    // Registration repeats until a Flex VITA packet arrives, then stops.
    int before = udp_count;
    flex_api_tick(6500);                       // fallback repeats every 5 s
    assert(udp_count > before);
    flex_api_rx_datagram(4992, pkt, words * 4);
    assert(m->meters_seen);
    before = udp_count;
    flex_api_tick(9000);
    assert(udp_count == before);
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

    // Through AetherSDR's relay: the port arrives right after READY, before
    // the radio's H line, and the client registers it with `client udpport`
    // (the radio ignores udp_register on a LAN) plus a 1-byte probe to 4992.
    flex_api_connected();
    sent_len = 0;
    sent[0] = '\0';
    udp_count = 0;
    flex_api_set_relay_udp_port(51234);
    assert(!strstr(sent, "udpport"));          // nothing before the handshake
    feed("V1.4.0.0\nH5A0B1C2D\n");
    expect_sent("|client udpport 51234\n");
    assert(udp_count == 1 && udp_port == 4992 && udp_last[0] == '\0');
    assert(!strstr(sent, "udp_register"));
    flex_api_tick(20000);
    assert(udp_count == 1);                    // no fallback registration
    // A port arriving after the handshake registers at once.
    flex_api_connected();
    sent_len = 0;
    sent[0] = '\0';
    feed("V1.4.0.0\nH5A0B1C2D\n");
    flex_api_set_relay_udp_port(40000);
    expect_sent("|client udpport 40000\n");
    flex_api_disconnected();
    puts("host_test: all checks passed");
    return 0;
}

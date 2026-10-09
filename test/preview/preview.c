// Renders the AetherKnob face on the host with sample radio data, so layout
// changes can be checked without the board. Build: make -C test/preview
#include "../../main/board.h"
#include "../../main/model.h"
#include "../../main/ui.h"
#include "../../main/usb_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int board_knob_take_detents(void) { return 0; }
bool board_knob_pressed(void) { return false; }
void usb_link_post_action(const knob_action_t *a) { (void)a; }

static uint32_t s_fb[480 * 480];

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    for (int y = a->y1; y <= a->y2; ++y)
        memcpy(&s_fb[y * 480 + a->x1], px + (size_t)(y - a->y1) * (size_t)(a->x2 - a->x1 + 1) * 4,
               (size_t)(a->x2 - a->x1 + 1) * 4);
    lv_display_flush_ready(d);
}

static void slice(knob_slice_t *s, char letter, double mhz, const char *mode, int lo, int hi,
                  float dbm, float peak, bool active, bool tx)
{
    *s = (knob_slice_t){.in_use = true, .letter = letter, .freq_mhz = mhz, .filter_lo = lo,
                        .filter_hi = hi, .active = active, .client_handle = 0x33333333,
                        .audio_level = 50, .agc_threshold = 65, .nr = true, .nr_level = 40,
                        .nb_level = 50, .anf_level = 30, .step = 100, .tx = tx,
                        .level_dbm = dbm, .level_peak_dbm = peak};
    snprintf(s->mode, sizeof s->mode, "%s", mode);
    snprintf(s->agc_mode, sizeof s->agc_mode, "med");
    snprintf(s->rxant, sizeof s->rxant, "ANT2");
}

int main(int argc, char **argv)
{
    const char *state = argc > 2 ? argv[2] : "bound";
    knob_model_t *m = model_get();
    if (strcmp(state, "waiting") == 0) {
        m->link = LINK_WAITING;
    } else {
        m->link = LINK_BOUND;
        m->gui_handle = 0x33333333;
        snprintf(m->gui_station, sizeof m->gui_station, "Arch");
        slice(&m->slices[0], 'A', 14.1725, "USB", 50, 3000, -95.0f, -75.0f, true, true);
        slice(&m->slices[1], 'B', 14.323, "USB", 100, 2800, -98.0f, -69.0f, false, false);
        m->swr = 1.4f; m->comp_db = 8.0f; m->fwd_watts = 0.0f; m->mic_db = -22.0f;
    }
    m->revision = 1;

    lv_init();
    lv_display_t *d = lv_display_create(480, 480);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    static uint8_t buf[480 * 480 * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    ui_start(d);
    lv_refr_now(d);

    FILE *f = fopen(argc > 1 ? argv[1] : "face.ppm", "wb");
    fprintf(f, "P6\n480 480\n255\n");
    for (int i = 0; i < 480 * 480; ++i) {
        uint8_t rgb[3] = {(uint8_t)(s_fb[i] >> 16), (uint8_t)(s_fb[i] >> 8), (uint8_t)s_fb[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

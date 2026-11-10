// The Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (board.h): ST77916 360x360 panel
// over QSPI, CST816 touch, a dial that only turns. Pins from Waveshare's
// schematic and its ESP-IDF demo (user_config.h).
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdatomic.h>

static const char *TAG = "board";

#define PIN_LCD_SCLK    13
#define PIN_LCD_CS      14
#define PIN_LCD_D0      15
#define PIN_LCD_D1      16
#define PIN_LCD_D2      17
#define PIN_LCD_D3      18
#define PIN_LCD_RST     21
#define PIN_BACKLIGHT   47
#define PIN_I2C_SDA     11
#define PIN_I2C_SCL     12
#define PIN_TP_INT      9
#define PIN_TP_RST      10
#define PIN_ENC_A       8   // pulses low once per detent turned clockwise
#define PIN_ENC_B       7   // ...and anticlockwise

#define TOUCH_ADDR      0x15
#define LCD_HOST        SPI2_HOST
#define LCD_SIZE        BOARD_LCD_SIZE
#define DRAW_LINES      (LCD_SIZE / 10)

// The dial is not a quadrature encoder: each detent pulls one of its two
// lines low for a few milliseconds, A going clockwise and B anticlockwise
// (Waveshare's "bidi switch knob"). Both lines are sampled every DIAL_TICK_US
// and a line counts once it has been low for DIAL_DEBOUNCE samples and comes
// back high. Flip DIAL_DIRECTION if clockwise turns tune down.
//
// Measured on the board: a slow single click sometimes leaves an echo, a
// second long pulse on the same line 50-65 ms after the first ends, as the
// knob settles back against the contact; both lines also glitch for 1 ms.
// So a line must read low DIAL_DEBOUNCE samples running, and the one pulse
// that starts within DIAL_ECHO_MS of the end of an isolated pulse (one that
// came DIAL_ISOLATED_MS or more after the line's previous pulse) is dropped.
// A fast spin is a stream of pulses, not isolated ones, so only its second
// detent can be lost.
#define DIAL_TICK_US      1000
#define DIAL_DEBOUNCE     3
#define DIAL_ECHO_MS      150
#define DIAL_ISOLATED_MS  250
#define DIAL_DIRECTION    (+1)

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_touch;
static esp_timer_handle_t s_dial_timer;
static atomic_int s_dial_count;

// The panel's ST77916 init sequence, verbatim from Waveshare's ESP-IDF demo
// (08_LVGL_Test) for this board, unrotated.
static const st77916_lcd_init_cmd_t s_lcd_init[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
};

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50000,   // Waveshare's; high enough not to sing
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

void board_backlight(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 255u / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_init(esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out)
{
    const spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_SCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        LCD_SIZE * DRAW_LINES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_cfg = ST77916_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    st77916_vendor_config_t vendor = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof s_lcd_init / sizeof s_lcd_init[0],
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io, &dev, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    *io_out = io;
    *panel_out = panel;
}

// The ST77916 takes windows on even pixel boundaries only (Waveshare's
// rounder): widen every redraw area to start on an even pixel and end on an
// odd one.
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

static void touch_init(void)
{
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TP_RST, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    const uint8_t normal_mode[2] = {0x00, 0x00};   // Waveshare's lcd_touch_init()
    i2c_master_transmit(s_touch, normal_mode, sizeof normal_mode, 50);
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t reg = 0x02;  // touch count, then X hi/lo, Y hi/lo
    uint8_t buf[5];
    if (i2c_master_transmit_receive(s_touch, &reg, 1, buf, sizeof buf, 20) == ESP_OK
        && (buf[0] & 0x0F) > 0) {
        data->point.x = ((buf[1] & 0x0F) << 8) | buf[2];
        data->point.y = ((buf[3] & 0x0F) << 8) | buf[4];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

typedef struct {
    gpio_num_t pin;
    int sign;
    int low_samples;
    int64_t low_since_ms;    // start of the current low pulse
    int64_t last_start_ms;   // start of the previous pulse
    int64_t echo_until_ms;   // a pulse starting before this is an echo
} dial_line_t;

static dial_line_t s_lines[2] = {
    {.pin = PIN_ENC_A, .sign = +1, .last_start_ms = INT64_MIN / 2},
    {.pin = PIN_ENC_B, .sign = -1, .last_start_ms = INT64_MIN / 2},
};

#if CONFIG_AETHERKNOB_BRINGUP
// Raw edges for tuning the decoder: (ms, line, level), dumped by board_dial_take_detents().
static struct { uint32_t ms; uint8_t line, level; } s_edges[64];
static atomic_int s_edge_w, s_edge_r;
static int s_last_level[2] = {1, 1};
#endif

static void dial_sample(void *arg)
{
    (void)arg;
    for (int i = 0; i < 2; ++i) {
        dial_line_t *l = &s_lines[i];
#if CONFIG_AETHERKNOB_BRINGUP
        int lv = gpio_get_level(l->pin);
        if (lv != s_last_level[i]) {
            s_last_level[i] = lv;
            int w = atomic_load(&s_edge_w);
            s_edges[w % 64].ms = (uint32_t)(esp_timer_get_time() / 1000);
            s_edges[w % 64].line = (uint8_t)i;
            s_edges[w % 64].level = (uint8_t)lv;
            atomic_store(&s_edge_w, w + 1);
        }
#endif
        const int64_t now = esp_timer_get_time() / 1000;
        if (gpio_get_level(l->pin) == 0) {
            if (l->low_samples == 0)
                l->low_since_ms = now;
            if (l->low_samples < DIAL_DEBOUNCE)
                ++l->low_samples;
        } else {
            if (l->low_samples >= DIAL_DEBOUNCE) {
                const int64_t start = l->low_since_ms;
                const bool echo = start < l->echo_until_ms;
                const bool isolated = start - l->last_start_ms >= DIAL_ISOLATED_MS;
                l->echo_until_ms = isolated && !echo ? now + DIAL_ECHO_MS : 0;
                l->last_start_ms = start;
                if (!echo)
                    atomic_fetch_add(&s_dial_count, DIAL_DIRECTION * l->sign);
            }
            l->low_samples = 0;
        }
    }
}

static void dial_init(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    const esp_timer_create_args_t t = {.callback = dial_sample, .name = "dial"};
    ESP_ERROR_CHECK(esp_timer_create(&t, &s_dial_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_dial_timer, DIAL_TICK_US));
}

int board_dial_take_detents(void)
{
#if CONFIG_AETHERKNOB_BRINGUP
    while (atomic_load(&s_edge_r) < atomic_load(&s_edge_w)) {
        int r = atomic_fetch_add(&s_edge_r, 1);
        ESP_LOGI(TAG, "edge %lu %c%s", (unsigned long)s_edges[r % 64].ms,
                 s_edges[r % 64].line ? 'B' : 'A', s_edges[r % 64].level ? "^" : "v");
    }
#endif
    return atomic_exchange(&s_dial_count, 0);
}

lv_display_t *board_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    i2c_device_config_t tp = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                              .device_address = TOUCH_ADDR, .scl_speed_hz = 300000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &tp, &s_touch));

    backlight_init();
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    panel_init(&io, &panel);
    touch_init();
    dial_init();

    const lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_SIZE * DRAW_LINES,
        .double_buffer = true,
        .hres = LCD_SIZE,
        .vres = LCD_SIZE,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_dma = 1, .swap_bytes = 1, .sw_rotate = 1},
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, NULL, TAG, "LVGL display failed");

    lvgl_port_lock(0);
    // The panel's own picture is upside down with the board the right way
    // up, and it ignores the MADCTL mirror bits, so LVGL turns the picture
    // 180 degrees; touches are turned with it.
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
    lv_indev_set_display(touch, disp);
    lvgl_port_unlock();

    board_backlight(80);
    ESP_LOGI(TAG, "board up");
    return disp;
}

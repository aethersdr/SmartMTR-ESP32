#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

// Pins from Elecrow's Board21.h for this board.
#define PIN_I2C_SDA     38
#define PIN_I2C_SCL     39
#define PIN_BACKLIGHT   6
#define PIN_ENC_A       42
#define PIN_ENC_B       4
#define PIN_SPI_CS      16
#define PIN_SPI_SCK     2
#define PIN_SPI_SDA     1
#define PIN_RGB_DE      40
#define PIN_RGB_VSYNC   7
#define PIN_RGB_HSYNC   15
#define PIN_RGB_PCLK    41

#define PCF8574_ADDR    0x21
#define PCF_TP_RST      (1u << 0)
#define PCF_TP_INT      (1u << 2)
#define PCF_LCD_POWER   (1u << 3)
#define PCF_LCD_RST     (1u << 4)
#define PCF_DIAL_CLICK  (1u << 5)   // input: written high, reads low while the dial is clicked down

#define TOUCH_ADDR      0x15
#define LCD_SIZE        480

// Quadrature edges per mechanical detent; tune on hardware if one detent
// of turning moves the VFO by more or less than one step.
#define DIAL_COUNTS_PER_DETENT 4

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_pcf;
static i2c_master_dev_handle_t s_touch;
static pcnt_unit_handle_t s_pcnt;
static uint8_t s_pcf_out;
static int s_dial_residue;

static esp_err_t pcf_write(uint8_t value)
{
    s_pcf_out = value;
    return i2c_master_transmit(s_pcf, &value, 1, 50);
}

static void pcf_set(uint8_t bits, bool high)
{
    pcf_write(high ? (s_pcf_out | bits) : (s_pcf_out & ~bits));
}

// The panel's ST7701S init sequence, verbatim from Arduino_GFX
// st7701_type5_init_operations as used by Elecrow for this board.
static const st7701_lcd_init_cmd_t s_lcd_init[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x00, 0x02}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x02, 0x13, 0x1B, 0x0D, 0x10, 0x05, 0x08, 0x07,
                       0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D}, 16, 0},
    {0xB1, (uint8_t[]){0x05, 0x13, 0x1B, 0x0D, 0x11, 0x05, 0x08, 0x07,
                       0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x43}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x43}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE5, (uint8_t[]){0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE8, (uint8_t[]){0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF,
                       0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x60}, 1, 0},
    {0x11, NULL, 0, 100},
    {0x29, NULL, 0, 50},
};

static void power_sequence(void)
{
    // Elecrow's order: LCD power, LCD reset pulse, touch reset pulse, INT high.
    pcf_write(PCF_DIAL_CLICK | PCF_TP_RST | PCF_LCD_RST);
    pcf_set(PCF_LCD_POWER, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    pcf_set(PCF_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    pcf_set(PCF_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    pcf_set(PCF_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    pcf_set(PCF_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    pcf_set(PCF_TP_INT, true);
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
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

static esp_lcd_panel_handle_t panel_init(void)
{
    spi_line_config_t line = {
        .cs_io_type = IO_TYPE_GPIO,
        .cs_gpio_num = PIN_SPI_CS,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = PIN_SPI_SCK,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = PIN_SPI_SDA,
        .io_expander = NULL,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_cfg = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line, 0);
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_cfg, &io));

    esp_lcd_rgb_panel_config_t rgb = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 12 * 1000 * 1000,
            .h_res = LCD_SIZE,
            .v_res = LCD_SIZE,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 20,
            .hsync_front_porch = 10,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 20,
            .vsync_front_porch = 10,
            .flags.pclk_active_neg = false,
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .bounce_buffer_size_px = LCD_SIZE * 10,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_RGB_HSYNC,
        .vsync_gpio_num = PIN_RGB_VSYNC,
        .de_gpio_num = PIN_RGB_DE,
        .pclk_gpio_num = PIN_RGB_PCLK,
        .disp_gpio_num = -1,
        // RGB565 bus order D0..D15. Elecrow's Arduino_GFX constructor lists
        // GPIO 46,3,8,18,17 as R and 5,45,48,47,21 as B, but on hardware that
        // mapping showed red and blue swapped (cyan as yellow, red as blue), so
        // the panel's red lines are the 5,45,... group: D0-D4 take 46,3,...
        .data_gpio_nums = {46, 3, 8, 18, 17, 14, 13, 12, 11, 10, 9, 5, 45, 48, 47, 21},
        .flags.fb_in_psram = 1,
    };
    st7701_vendor_config_t vendor = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof s_lcd_init / sizeof s_lcd_init[0],
        .rgb_config = &rgb,
        .flags.mirror_by_cmd = 1,
    };
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = -1,  // reset is on the PCF8574, pulsed in power_sequence()
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 18,
        .vendor_config = &vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io, &dev, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    return panel;
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

static void knob_init(void)
{
    pcnt_unit_config_t unit = {.low_limit = -1000, .high_limit = 1000};
    ESP_ERROR_CHECK(pcnt_new_unit(&unit, &s_pcnt));
    pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 1000};
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &filter));

    pcnt_chan_config_t a = {.edge_gpio_num = PIN_ENC_A, .level_gpio_num = PIN_ENC_B};
    pcnt_chan_config_t b = {.edge_gpio_num = PIN_ENC_B, .level_gpio_num = PIN_ENC_A};
    pcnt_channel_handle_t ca, cb;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &a, &ca));
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &b, &cb));
    // Standard x4 quadrature decode (IDF rotary encoder example).
    pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    gpio_pullup_en(PIN_ENC_A);
    gpio_pullup_en(PIN_ENC_B);
    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt));
}

int board_dial_take_detents(void)
{
    int count = 0;
    pcnt_unit_get_count(s_pcnt, &count);
    pcnt_unit_clear_count(s_pcnt);
    s_dial_residue += count;
    int detents = s_dial_residue / DIAL_COUNTS_PER_DETENT;
    s_dial_residue -= detents * DIAL_COUNTS_PER_DETENT;
    return detents;
}

bool board_dial_is_down(void)
{
    uint8_t in = 0xFF;
    if (i2c_master_receive(s_pcf, &in, 1, 20) != ESP_OK)
        return false;
    return (in & PCF_DIAL_CLICK) == 0;
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
    i2c_device_config_t pcf = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                               .device_address = PCF8574_ADDR, .scl_speed_hz = 400000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &pcf, &s_pcf));
    i2c_device_config_t tp = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                              .device_address = TOUCH_ADDR, .scl_speed_hz = 400000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &tp, &s_touch));

    backlight_init();
    power_sequence();
    esp_lcd_panel_handle_t panel = panel_init();
    knob_init();

    const lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = LCD_SIZE * LCD_SIZE,
        .double_buffer = true,
        .hres = LCD_SIZE,
        .vres = LCD_SIZE,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_spiram = 1, .direct_mode = 1},
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {.bb_mode = 1, .avoid_tearing = 1},
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(disp, NULL, TAG, "LVGL display failed");

    lvgl_port_lock(0);
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
    lv_indev_set_display(touch, disp);
    lvgl_port_unlock();

    board_backlight(80);
    ESP_LOGI(TAG, "board up");
    return disp;
}

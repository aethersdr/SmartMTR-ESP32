// AetherKnob: a FlexRadio controller on the Elecrow CrowPanel 2.1" rotary
// display, reaching the radio through AetherSDR's CTR2 USB relay.
#include "board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "model.h"
#include "ui.h"
#include "usb_link.h"

void app_main(void)
{
    model_lock();      // creates the model mutex before any other task runs
    model_unlock();

    lv_display_t *disp = board_init();
    lvgl_port_lock(0);
    ui_start(disp);
    lvgl_port_unlock();

    usb_link_start();
    ESP_LOGI("main", "AetherKnob running");
}

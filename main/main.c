// AetherKnob: a FlexRadio controller on the Waveshare ESP32-S3 knob
// (board.h), reaching the radio through AetherSDR's CTR2 USB relay.
#include "board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

#if CONFIG_AETHERKNOB_BRINGUP
    // The face polls the dial itself; here only touches are logged.
    ESP_LOGW("main", "bring-up mode: no USB link");
    for (bool was = false;;) {
        lvgl_port_lock(0);
        lv_indev_t *touch = lv_indev_get_next(NULL);
        bool down = touch && lv_indev_get_state(touch) == LV_INDEV_STATE_PRESSED;
        lv_point_t p = {0};
        if (touch)
            lv_indev_get_point(touch, &p);
        lvgl_port_unlock();
        if (down && !was)
            ESP_LOGI("main", "touch at %d,%d", (int)p.x, (int)p.y);
        was = down;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
#else
    usb_link_start();
    ESP_LOGI("main", "AetherKnob running");
#endif
}

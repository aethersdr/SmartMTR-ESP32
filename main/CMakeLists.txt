if(CONFIG_AETHERKNOB_BOARD_WAVESHARE_KNOB18)
    set(board_src board_waveshare.c)
else()
    set(board_src board_crowpanel.c)
endif()

idf_component_register(
    SRCS main.c ${board_src} usb_link.c ctr2_link.c flex_api.c model.c menu_items.c menu.c meter_ballistics.c ui.c
    INCLUDE_DIRS .
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_pcnt esp_driver_spi esp_lcd esp_timer)

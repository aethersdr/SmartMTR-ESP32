LVGL := ../../managed_components/lvgl__lvgl
SRCS := $(shell find $(LVGL)/src -name '*.c') preview.c ../../main/ui.c ../../main/model.c
CFLAGS := -O1 -w -I. -I$(LVGL) -DLV_CONF_INCLUDE_SIMPLE -DKNOB_HOST_TEST -I../../main

preview: $(SRCS) lv_conf.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

faces: preview
	./preview face.ppm bound && magick face.ppm face.png
	./preview waiting.ppm waiting && magick waiting.ppm waiting.png
	rm -f *.ppm

clean:
	rm -f preview *.ppm *.png
.PHONY: faces clean

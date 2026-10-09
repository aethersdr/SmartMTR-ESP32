LVGL := ../../managed_components/lvgl__lvgl
SRCS := $(shell find $(LVGL)/src -name '*.c') preview.c ../../main/ui.c ../../main/meter_ballistics.c ../../main/menu.c ../../main/menu_items.c ../../main/model.c ../../main/ctr2_link.c
CFLAGS := -O1 -w -I. -I$(LVGL) -DLV_CONF_INCLUDE_SIMPLE -DKNOB_HOST_TEST -I../../main

preview: $(SRCS) lv_conf.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

faces: preview
	./preview face.ppm bound && magick face.ppm face.png
	./preview peaks.ppm peaks && magick peaks.ppm peaks.png
	./preview tx.ppm tx && magick tx.ppm tx.png
	./preview live.ppm live && magick live.ppm live.png
	./preview span600.ppm span600 && magick span600.ppm span600.png
	./preview span12k.ppm span12k && magick span12k.ppm span12k.png
	./preview waiting.ppm waiting && magick waiting.ppm waiting.png
	./preview menu.ppm menu && magick menu.ppm menu.png
	./preview menu_mid.ppm menu_mid && magick menu_mid.ppm menu_mid.png
	./preview menu_edit.ppm menu_edit && magick menu_edit.ppm menu_edit.png
	./preview menu_tx.ppm menu_tx && magick menu_tx.ppm menu_tx.png
	rm -f *.ppm

touchtest: preview
	./preview /dev/null touchtest
	./preview /dev/null geomtest

clean:
	rm -f preview *.ppm *.png
.PHONY: faces touchtest clean

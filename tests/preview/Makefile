# Renders UI screenshots on the host: make -C tests/preview
# Writes build/*.ppm; convert them to PNG with any image tool. Fails if a
# row of a drop-down list wouldn't answer a tap.
CC      ?= gcc
SRC     := ../../source
OUT     := build
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -I$(SRC) -I../../third_party -I$(OUT)
UI_SRC  := scroll.c gfx.c font.c ui.c ui_buttons.c ui_library.c ui_settings.c settings.c theme.c background.c menu.c browser.c resume.c util.c image_impl.c library.c naming.c home.c libview.c watched.c playback.c stats.c ui_stats.c ui_details.c ui_live.c livetv.c jellyfin.c

.PHONY: run clean

run: $(OUT)/preview
	./$(OUT)/preview $(OUT)

$(OUT)/font_regular_ttf.c: ../../data/font_regular.ttf embed.py
	python3 embed.py $< $(OUT) font_regular_ttf

$(OUT)/font_bold_ttf.c: ../../data/font_bold.ttf embed.py
	python3 embed.py $< $(OUT) font_bold_ttf

$(OUT)/sub_%_ttf.c: ../../data/sub_%.ttf embed.py
	python3 embed.py $< $(OUT) sub_$*_ttf

SUB_FONTS := $(addprefix $(OUT)/sub_,$(addsuffix _ttf.c,atkinson comicneue fira lato ptserif))

$(OUT)/preview: preview.c sample_paths.h font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) $(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c $(SUB_FONTS)
	$(CC) $(CFLAGS) preview.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) \
		$(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c $(SUB_FONTS) -o $@ -lm -ljansson

clean:
	rm -rf $(OUT)

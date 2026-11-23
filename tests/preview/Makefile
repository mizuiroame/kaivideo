# Renders UI screenshots on the host: make -C tests/preview
# Writes build/*.ppm; convert them to PNG with any image tool. Fails if a
# row of a drop-down list wouldn't answer a tap.
CC      ?= gcc
SRC     := ../../source
OUT     := build
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -I$(SRC) -I../../third_party -I$(OUT)
UI_SRC  := scroll.c gfx.c font.c ui.c ui_buttons.c ui_library.c ui_settings.c settings.c theme.c background.c menu.c browser.c resume.c util.c image_impl.c library.c naming.c home.c libview.c watched.c playback.c stats.c ui_stats.c ui_details.c ui_live.c ui_iptvhome.c iptvhome.c iptv.c xtreamvod.c stremio.c ui_streams.c livetv.c jellyfin.c ui_downloads.c lang.c dav.c md4.c notice.c ui_notice.c

.PHONY: run clean

# Every language in data/ too, each in build/<id>: what no longer fits shows.
# For Japanese, Chinese and Korean, KAIVIDEO_CJK_FONT names a font with them
# (font_system_host.c); without one, their text draws as boxes.
LANGS   := $(patsubst ../../data/lang_%.txt,%,$(wildcard ../../data/lang_*.txt))

run: $(OUT)/preview
	./$(OUT)/preview $(OUT)
	@for l in $(LANGS); do mkdir -p $(OUT)/$$l && ./$(OUT)/preview $(OUT)/$$l ../../data/lang_$$l.txt || exit 1; done

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

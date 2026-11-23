# Host builds of the tests, with sanitizers:
#   make -C tests run
CC      ?= gcc
SRC     := ../source
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -fwrapv -I$(SRC) $(SAN) -I../third_party
LDFLAGS := $(SAN) -ljansson -lz -lm
OBJDIR  := build

TESTED  := resume.c browser.c util.c subs.c approot.c clock.c menu.c naming.c library.c home.c libview.c settings.c theme.c background.c gfx.c image_impl.c thumbconv.c watched.c trackprefs.c playback.c swframe.c asscompose.c blockcache.c jellyfin.c plex.c kinetic.c scroll.c hlslist.c hlsmaster.c webvtt.c stats.c trickplay.c livetv.c downloads.c lang.c dav.c md4.c update.c updaterelease.c notice.c xmlread.c iptv.c streamheaders.c xmltv.c iptvhome.c xtreamvod.c stremio.c
TESTS   :=test_main.c test_subs.c test_approot.c test_clock.c test_menu.c test_naming.c test_library.c test_home.c test_libview.c test_settings.c test_thumbs.c test_watched.c test_trackprefs.c test_playback.c test_swframe.c test_asscompose.c test_jellyfin.c test_plex.c test_kinetic.c test_stats.c test_trickplay.c test_livetv.c test_hlsmaster.c test_downloads.c test_lang.c test_dav.c test_update.c test_updaterelease.c test_notice.c test_iptv.c test_iptvhome.c test_xtreamvod.c test_stremio.c

.PHONY: run clean

# Without address randomization: ASan fails at start, now and then, on
# kernels that randomize with 32 bits (vm.mmap_rnd_bits).
run: $(OBJDIR)/test_main
	setarch $$(uname -m) -R ./$(OBJDIR)/test_main

$(OBJDIR)/test_main: $(TESTS) check.h $(addprefix $(SRC)/,$(TESTED))
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(TESTS) $(addprefix $(SRC)/,$(TESTED)) -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR)

# Host builds of the tests, with sanitizers:
#   make -C tests run
CC      ?= gcc
SRC     := ../source
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -fwrapv -I$(SRC) $(SAN) -I../third_party
LDFLAGS := $(SAN) -lm
OBJDIR  := build

TESTED  := resume.c browser.c util.c subs.c approot.c clock.c menu.c naming.c library.c home.c libview.c settings.c theme.c background.c gfx.c image_impl.c thumbconv.c
TESTS   := test_main.c test_subs.c test_approot.c test_clock.c test_menu.c test_naming.c test_library.c test_home.c test_libview.c test_settings.c test_thumbs.c

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

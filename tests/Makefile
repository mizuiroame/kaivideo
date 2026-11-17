# Host builds of the tests, with sanitizers:
#   make -C tests run
CC      ?= gcc
SRC     := ../source
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -fwrapv -I$(SRC) $(SAN)
LDFLAGS := $(SAN)
OBJDIR  := build

TESTED  := resume.c browser.c util.c
TESTS   := test_main.c

.PHONY: run clean

run: $(OBJDIR)/test_main
	./$(OBJDIR)/test_main

$(OBJDIR)/test_main: $(TESTS) check.h $(addprefix $(SRC)/,$(TESTED))
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(TESTS) $(addprefix $(SRC)/,$(TESTED)) -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR)

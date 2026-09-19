# Host-side unit tests. No board, no network — pure data and string work.
#   make        build and run every test
#   make test_format   build and run one
ROOT      := ../..
IDF_PATH  ?= $(HOME)/esp/esp-idf
CJSON_DIR := $(IDF_PATH)/components/json/cJSON

# Host-safe module sources: parsers and data tables only. Anything that touches
# esp_* headers belongs in a different file and is not linked here.
# main/net is NOT globbed wholesale: wifi.c / http_get.c / flight_source.c pull in
# esp_* headers and cannot build on the host. Only the deliberately-pure files are
# listed — the parsers, and the source-selection logic split out of flight_source
# precisely so its backoff and failover rules could be tested off-device.
MODULE_SRCS := $(wildcard $(ROOT)/main/data/*.c) \
               $(wildcard $(ROOT)/main/net/*_parse.c) \
               $(wildcard $(ROOT)/main/net/source_logic.c) \
               $(wildcard $(ROOT)/main/net/ota_policy.c)
SRCS        := $(MODULE_SRCS) $(CJSON_DIR)/cJSON.c

TESTS   := $(patsubst %.c,%,$(wildcard test_*.c))
BINDIR  := build

CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I$(ROOT)/main -I$(ROOT)/main/data -I$(ROOT)/main/net -I$(ROOT)/main/ui \
           -I$(CJSON_DIR) -I. \
           -DHOST_TEST=1 -DTEST_DIR=\"$(CURDIR)\"
LDFLAGS := -lm

.PHONY: all clean fontcheck stringcheck $(TESTS)
all: fontcheck stringcheck $(TESTS)

# A glyph missing from the subset renders as NOTHING in LVGL — no error, no
# placeholder. That is not something to discover on the panel, so it is checked
# here with everything else.
fontcheck:
	@echo "== font coverage"
	@python3 $(ROOT)/tools/check_font_coverage.py

# A German sentence written straight into a widget constructor never reaches
# the native speaker doing the M7 reading-aloud pass, and so never gets
# corrected. `--list` prints the whole lexicon for that pass.
stringcheck:
	@echo "== user-facing strings"
	@python3 $(ROOT)/tools/check_strings.py

$(TESTS): %: $(BINDIR)/%
	@echo "== $@"
	@$(BINDIR)/$@

# The prerequisites above name only .c files, so a change confined to a HEADER
# used to rebuild nothing and the suite reported green on code it had not
# compiled. Measured: changing VIEW_HERO_LEN from 48 to 6 in main/ui/
# view_model.h gave "2510 checks, 0 failed"; touching an unrelated .c then gave
# 882 failures. Struct fields, buffer lengths, #defines and static inlines all
# live in headers, so that is a large blind spot for a suite whose whole job is
# to catch exactly those.
#
# The second line writes the real prerequisite list. Note that it is a separate
# pass and not -MMD in CFLAGS: with one compile-and-link command over many
# sources, clang points every translation unit's -MMD at the same $(BINDIR)/%.d
# and the last one to finish overwrites the rest, so all but one source's
# headers are silently lost. -MM over the same source list emits one
# recipe-less rule per unit, all with the same target, and make unions their
# prerequisites — which is what is wanted.
# Makefile is a prerequisite too: a change to CFLAGS is as much a change to the
# binary as a change to a source file, and it is what makes an existing build
# tree pick the dependency files up the first time after this rule landed.
$(BINDIR)/%: %.c $(SRCS) Makefile | $(BINDIR)
	@$(CC) $(CFLAGS) -o $@ $< $(SRCS) $(LDFLAGS)
	@$(CC) $(CFLAGS) -MM -MP -MT $@ $< $(SRCS) > $(BINDIR)/$*.d

# -MP gave each header a recipe-less rule of its own, so deleting or renaming
# one is "no rule to make target" on the NEXT build instead of a hard error on
# this one.
-include $(BINDIR)/*.d

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	@rm -rf $(BINDIR)

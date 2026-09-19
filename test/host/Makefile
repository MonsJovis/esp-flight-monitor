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
               $(wildcard $(ROOT)/main/net/source_logic.c)
SRCS        := $(MODULE_SRCS) $(CJSON_DIR)/cJSON.c

TESTS   := $(patsubst %.c,%,$(wildcard test_*.c))
BINDIR  := build

CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I$(ROOT)/main -I$(ROOT)/main/data -I$(ROOT)/main/net -I$(ROOT)/main/ui \
           -I$(CJSON_DIR) -I. \
           -DHOST_TEST=1 -DTEST_DIR=\"$(CURDIR)\"
LDFLAGS := -lm

.PHONY: all clean fontcheck $(TESTS)
all: fontcheck $(TESTS)

# A glyph missing from the subset renders as NOTHING in LVGL — no error, no
# placeholder. That is not something to discover on the panel, so it is checked
# here with everything else.
fontcheck:
	@echo "== font coverage"
	@python3 $(ROOT)/tools/check_font_coverage.py

$(TESTS): %: $(BINDIR)/%
	@echo "== $@"
	@$(BINDIR)/$@

$(BINDIR)/%: %.c $(SRCS) | $(BINDIR)
	@$(CC) $(CFLAGS) -o $@ $< $(SRCS) $(LDFLAGS)

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	@rm -rf $(BINDIR)

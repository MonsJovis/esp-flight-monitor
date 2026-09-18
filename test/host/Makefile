# Host-side unit tests. No board, no network — pure data and string work.
#   make        build and run every test
#   make test_format   build and run one
ROOT      := ../..
IDF_PATH  ?= $(HOME)/esp/esp-idf
CJSON_DIR := $(IDF_PATH)/components/json/cJSON

# Host-safe module sources: parsers and data tables only. Anything that touches
# esp_* headers belongs in a different file and is not linked here.
MODULE_SRCS := $(wildcard $(ROOT)/main/data/*.c) $(wildcard $(ROOT)/main/net/*_parse.c)
SRCS        := $(MODULE_SRCS) $(CJSON_DIR)/cJSON.c

TESTS   := $(patsubst %.c,%,$(wildcard test_*.c))
BINDIR  := build

CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I$(ROOT)/main -I$(ROOT)/main/data -I$(ROOT)/main/net -I$(ROOT)/main/ui \
           -I$(CJSON_DIR) -I. \
           -DHOST_TEST=1 -DTEST_DIR=\"$(CURDIR)\"
LDFLAGS := -lm

.PHONY: all clean $(TESTS)
all: $(TESTS)

$(TESTS): %: $(BINDIR)/%
	@echo "== $@"
	@$(BINDIR)/$@

$(BINDIR)/%: %.c $(SRCS) | $(BINDIR)
	@$(CC) $(CFLAGS) -o $@ $< $(SRCS) $(LDFLAGS)

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	@rm -rf $(BINDIR)

# Host simulator for the radar screen: the real screen_radar.c, real LVGL 9.6,
# a 480x480 RGB565 framebuffer in memory, and a scripted touch panel.
#
#   make -C test/sim          build and run; screenshots land in test/sim/out/
#   SIM_DEBUG=1 build/sim_radar out    the same, with detail on stress failures
#   SIM_SEED=42 build/sim_radar out    the stress run with a different sky
#
# No board, no network. LVGL is compiled once into build/liblvgl.a (a minute
# or so the first time) and reused after that.
ROOT    := ../..
LVGL    := $(ROOT)/managed_components/lvgl__lvgl
BUILD   := build
OUT     := out
IDF_PATH  ?= $(HOME)/esp/esp-idf
CJSON_DIR := $(IDF_PATH)/components/json/cJSON

LVGL_SRCS := $(shell find $(LVGL)/src -name '*.c')
LVGL_OBJS := $(patsubst $(LVGL)/src/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRCS))

APP_SRCS := $(ROOT)/main/ui/screen_radar.c \
            $(wildcard $(ROOT)/main/ui/fonts/*.c) \
            $(wildcard $(ROOT)/main/data/*.c) \
            $(ROOT)/main/net/route_parse.c \
            $(CJSON_DIR)/cJSON.c

CFLAGS_COMMON := -std=gnu11 -O1 -g -DLV_CONF_INCLUDE_SIMPLE -DHOST_TEST=1 \
                 -I. -I$(LVGL) -I$(LVGL)/src
# The app code (screen, data, fonts) is built with AddressSanitizer and UBSan:
# an out-of-bounds index into a per-mark cache is exactly the bug a stress
# run exists to find, and without a sanitizer it would only draw something odd.
SAN        := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
APP_CFLAGS := $(CFLAGS_COMMON) $(SAN) -Wall -Wextra -Wno-unused-parameter \
              -Wno-missing-field-initializers \
              -Wno-deprecated-declarations \
              -I$(ROOT)/main -I$(ROOT)/main/ui -I$(ROOT)/main/ui/fonts \
              -I$(ROOT)/main/data -I$(ROOT)/main/net -I$(ROOT)/main/power \
              -I$(ROOT)/main/debug -I$(CJSON_DIR)

.PHONY: all run clean
all: run

$(BUILD)/lvgl/%.o: $(LVGL)/src/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS_COMMON) -w -c $< -o $@

$(BUILD)/liblvgl.a: $(LVGL_OBJS)
	@ar rcs $@ $^

$(BUILD)/sim_radar: sim_radar.c sim_common.h $(APP_SRCS) $(BUILD)/liblvgl.a lv_conf.h Makefile
	@$(CC) $(APP_CFLAGS) -o $@ sim_radar.c $(APP_SRCS) $(BUILD)/liblvgl.a -lm

# Leak detection off: LVGL's objects live until the process exits and are
# never freed, which LeakSanitizer (on by default on Linux, e.g. in CI)
# reports as a failure. Every other sanitizer finding is fatal.
run: $(BUILD)/sim_radar
	@mkdir -p $(OUT)
	@ASAN_OPTIONS=detect_leaks=0 $(BUILD)/sim_radar $(OUT)

clean:
	rm -rf $(BUILD) $(OUT)

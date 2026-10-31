/* The one ESP-IDF header main/ui/widget_input.c needs. A log line on the host
 * is a printf; anything it says shows up in the simulator's output. */
#pragma once
#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) printf("E %s: " fmt "\n", tag, ##__VA_ARGS__)

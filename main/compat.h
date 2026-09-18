/* Lets parser and table modules compile unchanged on the host test runner.
 * Device code gets the real esp_log.h; host tests get printf. */
#pragma once

#ifdef HOST_TEST
  #include <stdio.h>
  #define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
  #define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
  #define ESP_LOGI(tag, fmt, ...) printf("I %s: " fmt "\n", tag, ##__VA_ARGS__)
  #define ESP_LOGD(tag, fmt, ...) ((void)0)
#else
  #include "esp_log.h"
#endif

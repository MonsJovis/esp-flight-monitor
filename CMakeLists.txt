cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Carried into esp_app_desc_t and compared against the update manifest by
# main/net/ota_policy.c. Bump it in the same commit that publishes a build, or
# the device will decline the fix you sent it.
set(PROJECT_VER "0.1.0")

project(esp-flight-monitor)

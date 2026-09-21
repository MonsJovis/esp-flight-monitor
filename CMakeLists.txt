cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Carried into esp_app_desc_t and compared against the update manifest by
# main/net/ota_policy.c.
#
# The release workflow sets PROJECT_VER from the git tag, so a published build
# cannot disagree with the tag it was published under, and tools/check_release.py
# reads the version back out of the finished .bin to prove it. The literal below
# is only what a local build gets; it is deliberately NOT the release number,
# because a version that has to be remembered is a version that eventually is
# not — and the cost of forgetting is a device that declines the fix you sent it
# for the rest of its life (docs/DECISIONS.md D72).
#
# CMake reads this at CONFIGURE time and caches it, so changing PROJECT_VER in
# the environment does not by itself rebuild with the new value — an already
# configured build directory keeps the old one and says nothing. CI never sees
# this because it always starts from a clean checkout. Locally, delete
# build/CMakeCache.txt (or `idf.py fullclean`) when the version changes.
# tools/check_release.py catches it either way, by reading the version back out
# of the finished binary; it caught exactly this while the release pipeline was
# being built.
if(DEFINED ENV{PROJECT_VER})
    set(PROJECT_VER "$ENV{PROJECT_VER}")
else()
    set(PROJECT_VER "0.0.0-dev")
endif()

project(esp-flight-monitor)

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
if(DEFINED ENV{PROJECT_VER})
    set(PROJECT_VER "$ENV{PROJECT_VER}")
else()
    set(PROJECT_VER "0.0.0-dev")
endif()

project(esp-flight-monitor)

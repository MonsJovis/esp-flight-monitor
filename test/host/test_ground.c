/* Host tests for ground_filter.c (D83): which parked aircraft are shown. */
#include "test_util.h"

#include <string.h>

#include "ground_filter.h"

static ground_memory_t mem;

static aircraft_t ac(const char *hex, int32_t alt)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    strncpy(a.hex, hex, sizeof a.hex - 1);
    a.alt_ft = alt;
    return a;
}

static void test_rule(void)
{
    GROUP("airborne and unknown are always kept; a parked one never seen flying is not");
    ground_memory_reset(&mem);
    aircraft_t up = ac("440001", 3000), unk = ac("440002", ALT_UNKNOWN), park = ac("440003", ALT_GROUND);
    CHECK(ground_keep(&mem, &up, 100));
    CHECK(ground_keep(&mem, &unk, 100));
    CHECK(!ground_keep(&mem, &park, 100));

    GROUP("seen airborne, then on the ground: shown for ten minutes after, then not");
    aircraft_t landed = ac("440001", ALT_GROUND);
    CHECK(ground_keep(&mem, &landed, 112));
    CHECK(ground_keep(&mem, &landed, 100 + GROUND_SHOW_S));
    CHECK(!ground_keep(&mem, &landed, 101 + GROUND_SHOW_S));

    GROUP("an unknown altitude is not evidence of flight");
    aircraft_t unk_then_ground = ac("440002", ALT_GROUND);
    CHECK(!ground_keep(&mem, &unk_then_ground, 112));

    GROUP("no memory keeps everything; no hex on the ground is dropped");
    CHECK(ground_keep(NULL, &park, 100));
    aircraft_t anon = ac("", ALT_GROUND);
    CHECK(!ground_keep(&mem, &anon, 100));
}

static void test_capacity(void)
{
    GROUP("a busy sky reuses expired slots before it forgets a fresh landing");
    ground_memory_reset(&mem);
    char hex[8];
    /* A table full of aircraft seen long ago... */
    for (int i = 0; i < GROUND_MEM_SLOTS - 1; i++) {
        snprintf(hex, sizeof hex, "a%05d", i);
        aircraft_t a = ac(hex, 5000);
        ground_keep(&mem, &a, 10);
    }
    /* ...one that is landing now... */
    aircraft_t lander = ac("b00000", 800);
    ground_keep(&mem, &lander, 1000);
    /* ...and a new crowd, all airborne, exactly as many as have expired. */
    for (int i = 0; i < GROUND_MEM_SLOTS - 1; i++) {
        snprintf(hex, sizeof hex, "c%05d", i);
        aircraft_t a = ac(hex, 5000);
        ground_keep(&mem, &a, 1010);
    }
    aircraft_t down = ac("b00000", ALT_GROUND);
    CHECK(ground_keep(&mem, &down, 1020));
}

int main(void)
{
    test_rule();
    test_capacity();
    return test_summary();
}

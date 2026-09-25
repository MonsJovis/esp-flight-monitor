/* Which aircraft on the GROUND are worth showing (D83).
 *
 * The owner's rule: a plane on the ground is shown only if it departs in the
 * next ten minutes or landed in the last ten — where that is known. Over the
 * Vienna preset a third of the sky is parked at Schwechat (D77: 11 of 32), a
 * clutter of marks by the inner ring and rows in the list that answer nothing
 * he asked. "That plane up there" is never one of them.
 *
 * WHAT IS KNOWN, and it is only half of the rule. No feed this device can
 * reach has a schedule (D79): not adsb.lol, not adsb.im. So:
 *   - LANDED in the last ten minutes: known, by watching. An aircraft this
 *     device saw airborne, and now sees on the ground, has landed — within a
 *     poll (12 s) of the last airborne sighting. That time is remembered here.
 *   - DEPARTS in the next ten minutes: not knowable. A taxiing aircraft may be
 *     going out, coming in, or being towed, and a guess would be the kind of
 *     confident wrong answer this device is built not to give. It appears the
 *     moment it is airborne — seconds after its take-off roll.
 * Whatever is not known to have just landed stays hidden, including one that
 * landed before the device was switched on or moved.
 *
 * An unknown altitude is not "on the ground": kept, as it always was.
 *
 * Pure: no ESP-IDF, no clock of its own, no allocation. The caller owns the
 * memory (in PSRAM, flight_source.c) and says what time it is.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "flight_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How long after landing an aircraft on the ground is still shown. */
#define GROUND_SHOW_S     (10 * 60)

/* Aircraft seen airborne within the last GROUND_SHOW_S, the only ones worth
 * remembering. At 100 nm (the largest ring) the feed holds ~80 at once and
 * turns over perhaps fifty in ten minutes; an expired entry is reused before
 * anything live is evicted, so 160 leaves room. 12 bytes each. */
#define GROUND_MEM_SLOTS  160

typedef struct {
    char     hex[8];
    uint32_t airborne_s;   /* last poll that saw it airborne; 0 = free slot */
} ground_seen_t;

typedef struct {
    ground_seen_t slot[GROUND_MEM_SLOTS];
} ground_memory_t;

void ground_memory_reset(ground_memory_t *m);

/* Decides one aircraft from a fresh poll, and remembers it if it is airborne.
 * `now_s` must be > 0 and must not go backwards (seconds since boot will do).
 * Returns true to keep it. A NULL memory keeps everything. */
bool ground_keep(ground_memory_t *m, const aircraft_t *ac, uint32_t now_s);

#ifdef __cplusplus
}
#endif

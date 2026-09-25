/* See ground_filter.h for the rule and why half of it cannot be implemented. */
#include "ground_filter.h"

#include <string.h>

void ground_memory_reset(ground_memory_t *m)
{
    if (m != NULL) {
        memset(m, 0, sizeof *m);
    }
}

static ground_seen_t *find(ground_memory_t *m, const char *hex)
{
    for (int i = 0; i < GROUND_MEM_SLOTS; i++) {
        if (m->slot[i].airborne_s != 0 && strcmp(m->slot[i].hex, hex) == 0) {
            return &m->slot[i];
        }
    }
    return NULL;
}

static void remember(ground_memory_t *m, const char *hex, uint32_t now_s)
{
    ground_seen_t *e = find(m, hex);
    if (e == NULL) {
        /* A free or expired slot first: an entry older than the window can
         * never make an aircraft visible again, so it costs nothing to lose.
         * Only a sky busier than the table evicts a live one, and then the
         * oldest — the one nearest to expiring anyway. */
        int pick = 0;
        for (int i = 0; i < GROUND_MEM_SLOTS; i++) {
            const ground_seen_t *s = &m->slot[i];
            if (s->airborne_s == 0 || now_s - s->airborne_s > GROUND_SHOW_S) {
                pick = i;
                break;
            }
            if (s->airborne_s < m->slot[pick].airborne_s) {
                pick = i;
            }
        }
        e = &m->slot[pick];
        memset(e, 0, sizeof *e);
        strncpy(e->hex, hex, sizeof e->hex - 1);
    }
    e->airborne_s = now_s;
}

bool ground_keep(ground_memory_t *m, const aircraft_t *ac, uint32_t now_s)
{
    if (m == NULL || ac == NULL || now_s == 0) {
        return true;
    }
    if (ac->alt_ft != ALT_GROUND) {
        /* Airborne, or altitude unknown. Only a real altitude is evidence of
         * flight: an unknown one is kept, but it cannot later count as the
         * "airborne" half of a landing. */
        if (ac->alt_ft != ALT_UNKNOWN && ac->alt_ft >= 0 && ac->hex[0] != '\0') {
            remember(m, ac->hex, now_s);
        }
        return true;
    }
    if (ac->hex[0] == '\0') {
        return false;
    }
    const ground_seen_t *e = find(m, ac->hex);
    return e != NULL && now_s - e->airborne_s <= GROUND_SHOW_S;
}

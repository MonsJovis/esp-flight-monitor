/* Replays the real captured Gloggnitz traffic through the full on-device chain:
 * adsb_parse -> route_parse -> view_build -> screen_overhead.
 *
 * This exists because it exercises everything except the HTTP socket, with data
 * that actually flew over the house on 2026-09-18 — so the screens can be
 * verified on the panel before the device has ever joined a network. It is also
 * the only way to summon a specific state (no-route, empty sky) on demand
 * instead of waiting for the sky to produce one.
 */
#pragma once

/* n: 1 = an airliner with a route (§5.1)
 *    2 = a GA aircraft with no flight plan (§5.2)
 *    3 = empty sky (§5.3)
 *    4 = the longest destination name in the fixture, for the hero shrink ladder
 */
void dbg_fixture_show(int n);

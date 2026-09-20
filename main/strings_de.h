/* Every word the panel can show him. One file, no code.
 *
 * docs/PLAN.md M7: "All user-facing strings in one translation unit — audit
 * that nothing leaked into a widget constructor." This is that translation
 * unit, and tools/check_strings.py is the audit: it fails the host suite if a
 * literal a human would read appears anywhere in the main/ui screens or
 * main/data/view_build.c without being defined here.
 *
 * Why a header of #defines and not a table of indexed strings: there is
 * exactly one language. A gettext-shaped indirection would buy nothing and
 * cost a lookup and an ID that can go stale against its string. What M7
 * actually needs is a single page a native speaker can read top to bottom
 * without knowing any C — so that is what this is. `tools/check_strings.py
 * --list` prints it, plus the lexicons below, as plain text for exactly
 * that review.
 *
 * FOUR DELIBERATE EXCEPTIONS, all indexed tables rather than named constants,
 * all covered by host tests and all included in the --list dump:
 *
 *   main/data/fmt_de.c  — weekdays, months (Austrian: "Jänner", not "Januar"),
 *                         and the three compass tables. Arrays addressed by
 *                         index; naming 16 compass points as 16 macros and
 *                         then rebuilding an array out of them would be
 *                         strictly worse than the array.
 *   main/data/settings.c — the location preset display names ("Gloggnitz").
 *                         They sit in the preset table beside a POSIX TZ
 *                         string and a lat/lon; splitting the name out from
 *                         the row it belongs to would invite the two to drift.
 *   main/data/tbl_airport.c — 556 German city names, keyed on ICAO code. They
 *                         ARE the route headline (AGENTS.md §1), so they are a
 *                         language decision and not data; listed under
 *                         "Städtenamen in der Routenzeile".
 *   main/data/tbl_actype.c — the `size_class` field, a closed set of 22 German
 *                         words ("Mittelstreckenjet", "Segelflugzeug"), listed
 *                         under "Flugzeugklassen". The rest of that table is
 *                         manufacturer and model names, which are names.
 *
 * main/data/tbl_airline.c is NOT an exception in the same sense: an airline's
 * name is its name in every language, so --list prints a count and a note
 * rather than 206 entries.
 *
 * RULES FOR EDITING THIS FILE
 *
 *  - Austrian German, spoken register. If it would sound odd said aloud in
 *    Gloggnitz, it is wrong here even if it is textbook-correct German.
 *  - German uses "O" for Ost, never "E" (AGENTS.md §1, §10). Note that no
 *    compass point is defined in this file at all — all three screens that
 *    draw one now call compass_de_abbr(), so there is one table, not four.
 *  - Every glyph must exist in the font subset. "…", "„ “", a non-breaking
 *    space and a typographic apostrophe are NOT in it, and LVGL renders a
 *    missing glyph as nothing at all — no error, no placeholder, just shorter
 *    text. tools/check_font_coverage.py enforces this; believe it over your
 *    editor.
 *  - Non-ASCII is written as a hex escape with the codepoint named in a
 *    comment, so a grep for a glyph finds it and so no editor can silently
 *    re-encode it. Umlauts are the exception: they appear as literal UTF-8
 *    because the whole point of this file is that it can be read.
 */
#pragma once

/* ---- Shared: words more than one screen uses ------------------------- */

/* The one honest answer when nothing identifies the aircraft. Three screens
 * show it, and they must agree — see docs/DECISIONS.md D36, where the list
 * and the hero each grew a private version of the type-name chain and drifted
 * until the panel printed raw ICAO codes ("DIMO", "PA18") at him. */
#define STR_UNKNOWN_AIRCRAFT   "Unbekanntes Flugzeug"

#define STR_BACK               "Zurück"
#define STR_WLAN               "WLAN"

/* U+00B7 MIDDLE DOT, with a space each side: the separator between an
 * aircraft's identifier and its model ("RYR4MR · Boeing 737-800"). The same
 * dot the attribution line and the list already use, so one punctuation mark
 * means "and also" everywhere on this device. */
#define STR_ID_SEP             " \xC2\xB7 "

/* U+2014 EM DASH. "There is no value", never a blank slot: an empty space
 * reads as a broken device, a dash reads as a deliberate answer. */
#define STR_EM_DASH            "\xE2\x80\x94"

/* U+2192 RIGHTWARDS ARROW. Two different jobs, deliberately two names:
 * ROUTE is the magenta route glyph between origin and destination (§5.1),
 * ROW is the grey "this row goes somewhere" affordance in Einstellungen. */
#define STR_ROUTE_ARROW        "\xE2\x86\x92"
#define STR_ROW_ARROW          "\xE2\x86\x92"

/* ---- Über dir jetzt (DESIGN.md §5.1–5.3) ----------------------------- */

/* §5.2 amber caution tag, beside the reason sentence. */
#define STR_NO_FLIGHT_PLAN     "KEIN FLUGPLAN"

/* The route lookup is asked-but-unanswered for a few seconds. Saying
 * "KEIN FLUGPLAN" during those seconds is a lie the device then corrects in
 * front of him, and a panel he has caught lying is worse than no panel
 * (PLAN.md M4, the 2E0LXY lesson). */
#define STR_ROUTE_SEARCHING    "ROUTE WIRD GESUCHT"

/* Two cautions, because there are two problems and only one of them is his.
 *
 * KEIN NETZ  — not associated with any WiFi. He can walk over and look at the
 *              router, and that is worth telling him.
 * KEINE DATEN — associated, but the flight-data source has stopped answering.
 *              Nothing he can do, and it is still better than a panel quietly
 *              showing an empty sky as though it could see one.
 *
 * Both were "KEIN NETZ" until M8, which sent him to check a router that was
 * working perfectly. */
#define STR_NO_NETWORK_TAG     "KEIN NETZ"
#define STR_NO_DATA_TAG        "KEINE DATEN"

/* §5.3 caption above the last aircraft seen, so an empty sky still has
 * something true on it. */
#define STR_LAST_SEEN          "ZULETZT GESEHEN"

/* ---- Liste ------------------------------------------------------------ */

#define STR_HEADER_ONE         "1 Flugzeug in Reichweite"
#define FMT_HEADER_MANY        "%d Flugzeuge in Reichweite"

/* Row 0 is structurally always the nearest aircraft. The same words §5.1
 * uses for this exact aircraft, so the two screens name one thing one way. */
#define STR_TAG_NEAREST        "ÜBER DIR"

#define STR_EMPTY_SKY          "Der Himmel ist frei."

/* ---- Einstellungen ---------------------------------------------------- */

#define STR_HEADING_ORT            "Ort"
#define STR_HEADING_UMKREIS        "Umkreis"
#define STR_HEADING_HELLIGKEIT     "Helligkeit"
#define STR_HEADING_NACHTABSENKUNG "Nachtabsenkung"
#define STR_ACTIVE_TAG             "Aktiv"

/* km, not NM. The radius is stored and fetched in nautical miles because
 * that is what the API speaks, but "30 NM" means nothing to the man this is
 * built for and he has no reason to learn it. */
#define STR_UNIT_KM                "km"
#define STR_UNIT_PERCENT           "%"

/* "22:00 — 07:00". U+2014 EM DASH again, as the range separator. */
#define FMT_DIM_WINDOW             "%02d:00 \xE2\x80\x94 %02d:00"

/* The "Eigener Ort" coordinates, shown on that card until he has searched
 * for somewhere — after which the card shows the place's NAME instead,
 * because "Innsbruck · Tirol" tells him where the device thinks it is and
 * "47.2683°, 11.4008°" does not. U+00B0 DEGREE SIGN. The decimal point stays
 * a point: these are coordinates, not a quantity he reads. */
#define FMT_CUSTOM_COORDS          "%.4f\xC2\xB0, %.4f\xC2\xB0"

/* ---- Ortssuche (§5.8, screen_geo.c) ----------------------------------- */

/* The row in Einstellungen that opens the search, and the search screen's
 * own title. One wording for one thing: the row he taps and the screen he
 * lands on say the same words, so nothing has to be re-recognised. */
#define STR_GEO_TITLE              "Ort suchen"

/* The text field before he types. Not "Suchbegriff" — he is looking for a
 * place, and naming the thing is friendlier than naming the mechanism. */
#define STR_GEO_PLACEHOLDER        "Ort"

#define STR_GEO_BTN_SEARCH         "Suchen"

/* The status line, which is never blank (AGENTS.md §1), in the four states
 * it can be in. The WLAN screen above is built the same way and for the same
 * reason — a stale or empty line reads as "did that tap register?".
 *
 * NOTE on "..." — three ASCII periods, not U+2026. The font subset has no
 * ellipsis glyph and LVGL draws a missing glyph as nothing at all. Same trap,
 * same deliberate workaround as the WLAN block above. Do not "fix" them. */
#define STR_GEO_IDLE               "Noch nichts gesucht"
#define STR_GEO_SEARCHING          "Suche Orte..."
#define STR_GEO_ONE                "1 Ort gefunden"
#define FMT_GEO_MANY               "%d Orte gefunden"

/* He typed something real and the world has no such place. A sentence, not
 * "0 Treffer": the device tells him what happened in words everywhere else. */
#define STR_GEO_NONE               "Kein Ort mit diesem Namen"

/* The request did not come back. Deliberately about the SEARCH and not about
 * the network, because the flight data may well still be arriving — a line
 * that said "keine Verbindung" would contradict a radar that is visibly
 * working one screen away. */
#define STR_GEO_FAILED             "Die Suche hat nicht geantwortet"

/* Joins two German parts with U+00B7 MIDDLE DOT, the same "and also" this
 * device uses in every other list. It builds both of the search's display
 * lines (main/net/geo_parse.c): "Tirol · Österreich" under a result, and
 * "Innsbruck · Tirol" on the settings card afterwards. */
#define FMT_GEO_JOIN               "%s \xC2\xB7 %s"

/* Data attribution, required by licence and not decoration: adsb.lol's
 * position data is ODbL 1.0, and adsb.im supplies the routes (AGENTS.md
 * "Data licences" — "Show an attribution line in the UI"). It sits at the
 * foot of Einstellungen rather than on a screen he looks at daily, which is
 * where every map product puts its credit. The full ODbL notice, which is
 * longer than this panel can carry legibly, is in README.md.
 * U+00B7 MIDDLE DOT as the separator, matching every other list on the
 * device. */
#define STR_ATTRIBUTION            "Flugdaten adsb.lol (ODbL) \xC2\xB7 Routen adsb.im"

/* A SECOND line, added when the place search (§5.8) did. Open-Meteo's
 * geocoding data is GeoNames under CC BY 4.0, which requires attribution the
 * same way adsb.lol's ODbL does — so it is named for the same reason and in
 * the same place, not because the line looked short.
 *
 * Two lines rather than one longer one: at plex_mono_12 the combined string
 * is about 470 px and the content column is 440, so a single line would have
 * been truncated — which for an attribution is not a cosmetic problem. */
#define STR_ATTRIBUTION_2          "Orte Open-Meteo \xC2\xB7 GeoNames (CC BY 4.0)"

/* ---- Akku (main/power/battery_policy.c) ------------------------------- */

/* "Akku", never "Batterie". In Austrian everyday speech a Batterie is the
 * kind you throw away and an Akku is the kind that charges, and this one
 * charges. */
#define STR_HEADING_AKKU       "Akku"

/* The honest answer on a device with no cell fitted, which is every device
 * until one is. It is also the line that answers "did the plug go in
 * properly?" on the day one is, which is why the row exists at all. */
#define STR_BATTERY_NONE       "Kein Akku"

/* WHOLE LINES with the percentage in them, for the same reason the WLAN
 * block above is written that way: a fragment like " wird geladen" tells the
 * native speaker reading this file down nothing about the sentence he is
 * being asked to judge.
 *
 * U+00B7 MIDDLE DOT as the separator, the same "and also" this device uses
 * everywhere else. The space before the percent sign is German typography
 * and matches the Helligkeit read-out on the settings screen. */
#define FMT_BATTERY_CHARGING   "%d %% \xC2\xB7 wird geladen"
#define FMT_BATTERY_FULL       "%d %% \xC2\xB7 voll geladen"
#define FMT_BATTERY_RUNNING    "%d %% \xC2\xB7 läuft mit Akku"

/* Plugged in, a cell fitted, and the charger doing nothing. Deliberately
 * blunt rather than reassuring: this is what a blocked charger looks like,
 * and a device that quietly never charges is the failure this line exists
 * to make impossible to miss. */
#define FMT_BATTERY_IDLE       "%d %% \xC2\xB7 wird nicht geladen"

/* The chrome badge, shown only while he is actually running on the cell.
 * Upper case like every other tag on this device (KEIN NETZ, ÜBER DIR) —
 * those are the device reporting a condition, and so is this. */
#define FMT_BATTERY_BADGE      "AKKU %d %%"

/* ---- Einheiten und Datum (main/data/fmt_de.c) ------------------------- */

/* The unit WORDS. The numbers they hang off are built by fmt_dec1_de() and
 * fmt_int_de() with a German decimal comma and thousands dot; only the unit
 * itself is language, so only the unit is here. */
#define FMT_KM                 "%s km"
#define FMT_METRES             "%s m"

/* An aircraft on the ground has no altitude worth printing in metres. */
#define STR_ON_GROUND          "am Boden"

/* "Freitag, 18. September 2026". The ORDER is the language decision — day
 * name, then day number with its ordinal point, then month, then year — and
 * it is here so a reviewer reading this file aloud sees it rather than
 * having to reconstruct it from a printf in another file. The weekday and
 * month names themselves are the two indexed tables in fmt_de.c. */
#define FMT_DATE_DE            "%s, %d. %s %d"

/* ---- Kompassband (widget_compass.c) ----------------------------------- */

/* U+00B0 DEGREE SIGN. The numeric bearing under the tape. */
#define FMT_DEGREES                "%d\xC2\xB0"

/* ---- WLAN ------------------------------------------------------------- */

/* NOTE on "..." below: the font subset covers U+002E FULL STOP but NOT
 * U+2026 HORIZONTAL ELLIPSIS. A real "…" would render as nothing at all, so
 * every "..." here is three ASCII periods on purpose. Do not "fix" them. */
#define STR_WIFI_SCANNING          "Suche Netzwerke..."
/* WHOLE SENTENCES with a %s where the network name goes, not a prefix and a
 * suffix to be glued together at the call site. Both spellings work on the
 * panel; only one of them works on the page. `check_strings.py --list` prints
 * this file for a native speaker to read down, and a fragment like " wird
 * hergestellt..." — or, worse, a lone "..." — tells that reader nothing about
 * the sentence he is being asked to judge. A translation unit that cannot be
 * read aloud is not serving the purpose it was gathered for (PLAN.md M7). */
#define FMT_WIFI_CONNECTED         "Verbunden mit %s"
#define STR_WIFI_CONNECTED_GEN     "Verbunden"                   /* connected, no ssid to name */
#define FMT_WIFI_FAILED            "Verbindung fehlgeschlagen: %s"
#define STR_WIFI_IDLE              "Nicht verbunden"
/* "Verbinde mit X...", not "Verbindung zu X wird hergestellt..." — the
 * screen above it already says "Suche Netzwerke...", so the passive form
 * put two different voices on one screen: a machine reporting on itself,
 * and the device talking to him. It talks to him everywhere else on this
 * device ("Ich suche ein bekanntes WLAN"), so it talks to him here. */
#define FMT_WIFI_CONNECTING        "Verbinde mit %s..."

/* DO-257A: colour is never the only carrier of meaning, so the word ships
 * beside the green tick rather than instead of it. */
#define STR_WIFI_SAVED             "gespeichert"

#define STR_WIFI_LIST_EMPTY        "Keine Netzwerke gefunden"
#define STR_WIFI_BTN_RESCAN        "Suchen"

#define FMT_WIFI_PW_NETWORK        "Verbindung mit %s"  /* so he can see what he is joining */
#define STR_WIFI_PW_PLACEHOLDER    "Passwort"
#define STR_WIFI_PW_SHOW           "Anzeigen"         /* while hidden — names the action the tap performs */
#define STR_WIFI_PW_HIDE           "Verbergen"        /* while visible */
#define STR_WIFI_PW_CONNECT        "Verbinden"
#define STR_WIFI_PW_CANCEL         "Abbrechen"

/* ---- Sätze: the explanations, built in main/data/view_build.c ---------- */

/* §5.2's whole argument is that a missing route is not a failure to hide but
 * a fact to explain. Seven of the thirteen aircraft in our own live capture
 * were GA with no flight plan — and those are precisely the ones he HEARS,
 * low and slow over the house. A blank slot reads as broken; a sentence
 * reads as informative. Each of these answers "why does it not say where
 * this one is going", in the register a person would answer it. */
/* NOT "nur bei Linienflügen". A Linienflug is specifically scheduled, regular
 * public transport — a Charterflug or Bedarfsflug is explicitly not one, and
 * charter, cargo and ambulance flights all have routes. The sentence was
 * simply false.
 *
 * What is actually true is narrower and plainer: the route lookup is keyed on
 * the CALLSIGN, so a route exists exactly when the aircraft is flying under a
 * flight number. He knows what a Flugnummer is; he has read one off a ticket
 * his whole life. */
#define STR_REASON_GA          "Eine Route gibt es nur zu Flügen mit Flugnummer."
#define STR_REASON_MIL         "Militärflüge scheinen in keinem öffentlichen Flugplan auf."

/* There is no sentence for helicopters, on purpose. One used to be here and
 * the owner had it removed: it told his father-in-law something he has known
 * his whole life, and a panel that explains what its reader already knows is
 * a panel he stops reading. Helicopters get STR_REASON_NONE below — the fact
 * without the lesson. Do not put it back without asking him. */

/* Not "there is no flight plan" — we do not know that. We know we cannot get
 * it right now, and the sentence says only that. */
#define STR_REASON_UNAVAILABLE "Der Flugplan ist im Moment nicht verfügbar."

/* Not "liegt keine Routeninformation vor" — that is Amtsdeutsch, the
 * register of a form he has to fill in. He is standing in his garden. */
#define STR_REASON_NONE        "Zu diesem Flug ist keine Route bekannt."
#define STR_REASON_SEARCHING   "Die Route wird noch gesucht."

/* Empty sky before the clock is set. The device would otherwise show
 * "Donnerstag, 1. Jänner 1970" in 100 px type, which is what a broken
 * appliance looks like — on the first screen he ever sees. An honest
 * sentence is worth more than a time we cannot justify. */
#define STR_NO_NETWORK_HERO    "Kein Netz"
#define STR_NO_NETWORK_SUB     "Ich suche ein bekanntes WLAN."

/* Placeholder clock, shown for the same reason. */
#define STR_CLOCK_UNSET        "--:--"

/* Lowercase "unbekannt" reads as a value in a data row; STR_UNKNOWN_AIRCRAFT
 * is a headline. Both are needed and they are not interchangeable. */
#define STR_UNKNOWN_VALUE      "unbekannt"

/* ICAO aircraft-type designator -> structured entry, keyed on the `t` field
 * from adsb.lol / adsb.fi (e.g. "A20N", "DV20", "EC35").
 *
 * `category` (see ac_type_t in flight_types.h) drives DESIGN.md §5.2: it is
 * what lets the "Ohne Route" screen say *why* an aircraft has no route
 * instead of leaving a blank slot. AC_CAT_PRIVATE means "no route is normal",
 * not an error -- getting the airliner/private split right matters more than
 * the exact wording of any one entry.
 *
 * COVERAGE IS THE POINT here, not completeness for its own sake. The table
 * started out weighted towards airliners, and a Cessna 177 over Gloggnitz came
 * out of hero_from_type() as the bare designator C177 in 76 px type -- the
 * exact failure AGENTS.md §1 forbids and the one docs/DECISIONS.md D36 had
 * already removed from the list screen. view_build.c now says "Unbekanntes
 * Flugzeug" rather than a code, which is honest but is still not the answer;
 * the answer is "Cessna 177 Cardinal", and the only way to have it is to carry
 * the row. So the light-GA, glider, helicopter and ultralight rows below are
 * not padding. That is the traffic he actually hears -- low and slow over the
 * house -- and every row missing here is one more "Unbekanntes Flugzeug".
 *
 * A few designators name a CLASS rather than a model: GLID, BALL, GYRO, SHIP,
 * ULAC. Their four text fields collapse onto the same German word because that
 * word is genuinely all the designator says. The manufacturer field still has
 * to hold it rather than a dash -- a dash is what actype_is_placeholder()
 * below reads as "nobody identified this", and it would send
 * actype_display_name() down to the emitter category, which for a glider
 * squawking A1 answers "Leichtflugzeug".
 *
 * Latin-1 only, in every string. The three hero faces carry ASCII plus
 * U+00C0-U+00FF and nothing else (CORE_RANGES in tools/build_fonts.sh), and
 * tools/check_font_coverage.py fails the build on anything outside that.
 *
 * Two fixture entries are not real ICAO Doc 8643 type designators; see the
 * comments by "TWR" and "G2CA" below for the judgement call on each.
 *
 * Sorted ascending by ICAO type designator for bsearch -- test_tables.c
 * checks sortedness and uniqueness directly against tbl_actype_entries().
 */
#include "tables.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const actype_lookup_t ACTYPES[] = {
    {"A109", {"Leonardo", "A109", "Leonardo (Agusta) A109", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A119", {"Leonardo", "A119", "Leonardo A119 Koala", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A124", {"Antonov", "An-124", "Antonov An-124 Ruslan", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"A139", {"Leonardo", "AW139", "Leonardo AW139", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A169", {"Leonardo", "AW169", "Leonardo AW169", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A189", {"Leonardo", "AW189", "Leonardo AW189", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A19N", {"Airbus", "A319neo", "Airbus A319neo", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A20N", {"Airbus", "A320neo", "Airbus A320neo", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A21N", {"Airbus", "A321neo", "Airbus A321neo", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A306", {"Airbus", "A300-600", "Airbus A300-600", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A30B", {"Airbus", "A300B", "Airbus A300B", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A310", {"Airbus", "A310", "Airbus A310", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A318", {"Airbus", "A318", "Airbus A318", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A319", {"Airbus", "A319", "Airbus A319", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A320", {"Airbus", "A320", "Airbus A320", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A321", {"Airbus", "A321", "Airbus A321", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A332", {"Airbus", "A330-200", "Airbus A330-200", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A333", {"Airbus", "A330-300", "Airbus A330-300", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A339", {"Airbus", "A330-900", "Airbus A330-900neo", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A342", {"Airbus", "A340-200", "Airbus A340-200", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A343", {"Airbus", "A340-300", "Airbus A340-300", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A345", {"Airbus", "A340-500", "Airbus A340-500", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A346", {"Airbus", "A340-600", "Airbus A340-600", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A359", {"Airbus", "A350-900", "Airbus A350-900", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A35K", {"Airbus", "A350-1000", "Airbus A350-1000", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A388", {"Airbus", "A380-800", "Airbus A380", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"A400", {"Airbus", "A400M", "Airbus A400M Atlas", "Militärflugzeug", AC_CAT_MILITARY}},
    {"AA1", {"Grumman American", "AA-1", "Grumman AA-1 Yankee", "Zweisitzer", AC_CAT_PRIVATE}},
    {"AA5", {"Grumman American", "AA-5", "Grumman AA-5 Tiger", "Viersitzer", AC_CAT_PRIVATE}},
    {"AC11", {"Rockwell", "Commander 114", "Rockwell Commander 114", "Viersitzer", AC_CAT_PRIVATE}},
    {"AH64", {"Boeing", "AH-64", "Boeing AH-64 Apache", "Hubschrauber", AC_CAT_MILITARY}},
    {"AN12", {"Antonov", "An-12", "Antonov An-12", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"AN2", {"Antonov", "An-2", "Antonov An-2", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"AN24", {"Antonov", "An-24", "Antonov An-24", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AN26", {"Antonov", "An-26", "Antonov An-26", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AQUI", {"Aquila", "AT01", "Aquila AT01", "Zweisitzer", AC_CAT_PRIVATE}},
    {"ARCU", {"Schempp-Hirth", "Arcus", "Schempp-Hirth Arcus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"ARJ2", {"COMAC", "ARJ21", "COMAC ARJ21", "Regionaljet", AC_CAT_AIRLINER}},
    {"AS21", {"Schleicher", "ASK 21", "Schleicher ASK 21", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"AS32", {"Airbus Helicopters", "AS332", "Airbus AS332 Super Puma", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"AS50", {"Airbus Helicopters", "AS350", "Airbus Helicopters AS350 Ecureuil", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"AS55", {"Airbus Helicopters", "AS355", "Airbus Helicopters AS355 TwinStar", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"AS65", {"Airbus Helicopters", "AS365", "Airbus AS365 Dauphin", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"AT3", {"Aero", "AT-3", "Aero AT-3", "Zweisitzer", AC_CAT_PRIVATE}},
    {"AT43", {"ATR", "42-300", "ATR 42-300", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AT45", {"ATR", "42-500", "ATR 42-500", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AT72", {"ATR", "72", "ATR 72", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AT75", {"ATR", "ATR 72-500", "ATR 72-500", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AT76", {"ATR", "ATR 72-600", "ATR 72-600", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"AW09", {"Leonardo", "AW009", "Leonardo AW009", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B06", {"Bell", "206", "Bell 206 JetRanger", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B190", {"Beechcraft", "1900D", "Beechcraft 1900D", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"B212", {"Bell", "212", "Bell 212", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B350", {"Beechcraft", "King Air 350", "Beechcraft King Air 350", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"B37M", {"Boeing", "737 MAX 7", "Boeing 737 MAX 7", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B38M", {"Boeing", "737 MAX 8", "Boeing 737 MAX 8", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B39M", {"Boeing", "737 MAX 9", "Boeing 737 MAX 9", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B3XM", {"Boeing", "737 MAX 10", "Boeing 737 MAX 10", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B407", {"Bell", "407", "Bell 407", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B412", {"Bell", "412", "Bell 412", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B429", {"Bell", "429", "Bell 429", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B461", {"British Aerospace", "BAe 146-100", "BAe 146-100", "Regionaljet", AC_CAT_AIRLINER}},
    {"B462", {"British Aerospace", "BAe 146-200", "BAe 146-200", "Regionaljet", AC_CAT_AIRLINER}},
    {"B463", {"British Aerospace", "BAe 146-300", "BAe 146-300", "Regionaljet", AC_CAT_AIRLINER}},
    {"B505", {"Bell", "505", "Bell 505", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B52", {"Boeing", "B-52", "Boeing B-52 Stratofortress", "Militärflugzeug", AC_CAT_MILITARY}},
    {"B722", {"Boeing", "727-200", "Boeing 727-200", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B732", {"Boeing", "737-200", "Boeing 737-200", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B733", {"Boeing", "737-300", "Boeing 737-300", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B734", {"Boeing", "737-400", "Boeing 737-400", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B735", {"Boeing", "737-500", "Boeing 737-500", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B736", {"Boeing", "737-600", "Boeing 737-600", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B737", {"Boeing", "737-700", "Boeing 737-700", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B738", {"Boeing", "737-800", "Boeing 737-800", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B739", {"Boeing", "737-900", "Boeing 737-900", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B741", {"Boeing", "747-100", "Boeing 747-100", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B742", {"Boeing", "747-200", "Boeing 747-200", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B743", {"Boeing", "747-300", "Boeing 747-300", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B744", {"Boeing", "747-400", "Boeing 747-400", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B748", {"Boeing", "747-8", "Boeing 747-8", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B752", {"Boeing", "757-200", "Boeing 757-200", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B753", {"Boeing", "757-300", "Boeing 757-300", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B762", {"Boeing", "767-200", "Boeing 767-200", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B763", {"Boeing", "767-300", "Boeing 767-300", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B764", {"Boeing", "767-400", "Boeing 767-400", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B772", {"Boeing", "777-200", "Boeing 777-200", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B773", {"Boeing", "777-300", "Boeing 777-300", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B77L", {"Boeing", "777F", "Boeing 777 Freighter", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"B77W", {"Boeing", "777-300ER", "Boeing 777-300ER", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B788", {"Boeing", "787-8", "Boeing 787-8 Dreamliner", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B789", {"Boeing", "787-9", "Boeing 787-9 Dreamliner", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"B78X", {"Boeing", "787-10", "Boeing 787-10 Dreamliner", "Großraumflugzeug", AC_CAT_AIRLINER}},
    /* "Ballon", not "Heißluftballon": Doc 8643's BALL is the GENERIC balloon
     * designator, so the warmer word would confidently mislabel a gas
     * balloon. It also matches ac_category_de()'s word for emitter category
     * B2, so the device says one thing one way (D36). */
    {"BALL", {"Ballon", "Ballon", "Ballon", "Ballon", AC_CAT_PRIVATE}},
    {"BCS1", {"Airbus", "A220-100", "Airbus A220-100", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"BCS3", {"Airbus", "A220-300", "Airbus A220-300", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"BE10", {"Beechcraft", "King Air 100", "Beechcraft King Air 100", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"BE20", {"Beechcraft", "King Air 200", "Beechcraft King Air 200", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"BE23", {"Beechcraft", "Musketeer", "Beechcraft Musketeer", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE24", {"Beechcraft", "Sierra", "Beechcraft Sierra", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE30", {"Beechcraft", "King Air 300", "Beechcraft King Air 300", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"BE33", {"Beechcraft", "Debonair", "Beechcraft Debonair", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE35", {"Beechcraft", "Bonanza 35", "Beechcraft Bonanza 35", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE36", {"Beechcraft", "Bonanza", "Beechcraft Bonanza", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE40", {"Beechcraft", "Beechjet 400", "Beechcraft Beechjet 400", "Privatjet", AC_CAT_PRIVATE}},
    {"BE55", {"Beechcraft", "Baron 55", "Beechcraft Baron 55", "Sechssitzer", AC_CAT_PRIVATE}},
    {"BE58", {"Beechcraft", "Baron 58", "Beechcraft Baron 58", "Sechssitzer", AC_CAT_PRIVATE}},
    {"BE60", {"Beechcraft", "Duke", "Beechcraft Duke", "Sechssitzer", AC_CAT_PRIVATE}},
    {"BE76", {"Beechcraft", "Duchess", "Beechcraft Duchess", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE99", {"Beechcraft", "99 Airliner", "Beechcraft 99 Airliner", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"BE9L", {"Beechcraft", "King Air 90", "Beechcraft King Air 90", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"BN2P", {"Britten-Norman", "BN-2", "Britten-Norman Islander", "Kleinflugzeug", AC_CAT_PRIVATE}},
    {"C120", {"Cessna", "120", "Cessna 120", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C130", {"Lockheed", "C-130", "Lockheed C-130 Hercules", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C140", {"Cessna", "140", "Cessna 140", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C150", {"Cessna", "150", "Cessna 150", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C152", {"Cessna", "152", "Cessna 152", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C160", {"Transall", "C-160", "Transall C-160", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C162", {"Cessna", "162", "Cessna 162 Skycatcher", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C17", {"Boeing", "C-17", "Boeing C-17 Globemaster III", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C170", {"Cessna", "170", "Cessna 170", "Viersitzer", AC_CAT_PRIVATE}},
    {"C172", {"Cessna", "172", "Cessna 172 Skyhawk", "Viersitzer", AC_CAT_PRIVATE}},
    {"C175", {"Cessna", "175", "Cessna 175 Skylark", "Viersitzer", AC_CAT_PRIVATE}},
    {"C177", {"Cessna", "177", "Cessna 177 Cardinal", "Viersitzer", AC_CAT_PRIVATE}},
    {"C180", {"Cessna", "180", "Cessna 180 Skywagon", "Viersitzer", AC_CAT_PRIVATE}},
    {"C182", {"Cessna", "182", "Cessna 182 Skylane", "Viersitzer", AC_CAT_PRIVATE}},
    {"C185", {"Cessna", "185", "Cessna 185 Skywagon", "Viersitzer", AC_CAT_PRIVATE}},
    {"C188", {"Cessna", "188", "Cessna 188 Ag Wagon", "Agrarflugzeug", AC_CAT_PRIVATE}},
    {"C195", {"Cessna", "195", "Cessna 195 Businessliner", "Viersitzer", AC_CAT_PRIVATE}},
    {"C205", {"Cessna", "205", "Cessna 205", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C206", {"Cessna", "206", "Cessna 206 Stationair", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C207", {"Cessna", "207", "Cessna 207 Stationair", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C208", {"Cessna", "Caravan", "Cessna 208 Caravan", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"C210", {"Cessna", "210", "Cessna 210 Centurion", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C212", {"CASA", "C-212", "CASA C-212 Aviocar", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"C25A", {"Cessna", "Citation CJ2", "Cessna Citation CJ2", "Privatjet", AC_CAT_PRIVATE}},
    {"C25B", {"Cessna", "Citation CJ3", "Cessna Citation CJ3", "Privatjet", AC_CAT_PRIVATE}},
    {"C25C", {"Cessna", "Citation CJ4", "Cessna Citation CJ4", "Privatjet", AC_CAT_PRIVATE}},
    {"C25M", {"Cessna", "Citation M2", "Cessna Citation M2", "Privatjet", AC_CAT_PRIVATE}},
    {"C27J", {"Leonardo", "C-27J", "Leonardo C-27J Spartan", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C295", {"Airbus", "C295", "Airbus C295", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C303", {"Cessna", "303", "Cessna 303 Crusader", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C30J", {"Lockheed Martin", "C-130J", "Lockheed C-130J Hercules", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C310", {"Cessna", "310", "Cessna 310", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C337", {"Cessna", "337", "Cessna 337 Skymaster", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C340", {"Cessna", "340", "Cessna 340", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C402", {"Cessna", "402", "Cessna 402", "Kleinflugzeug", AC_CAT_PRIVATE}},
    {"C404", {"Cessna", "404", "Cessna 404 Titan", "Kleinflugzeug", AC_CAT_PRIVATE}},
    {"C414", {"Cessna", "414", "Cessna 414 Chancellor", "Kleinflugzeug", AC_CAT_PRIVATE}},
    {"C42", {"Ikarus", "C42", "Ikarus C42", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C421", {"Cessna", "421", "Cessna 421 Golden Eagle", "Kleinflugzeug", AC_CAT_PRIVATE}},
    {"C425", {"Cessna", "425", "Cessna 425 Conquest I", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"C441", {"Cessna", "441", "Cessna 441 Conquest II", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"C500", {"Cessna", "Citation I", "Cessna Citation I", "Privatjet", AC_CAT_PRIVATE}},
    {"C510", {"Cessna", "Citation Mustang", "Cessna Citation Mustang", "Privatjet", AC_CAT_PRIVATE}},
    {"C525", {"Cessna", "CitationJet", "Cessna CitationJet", "Privatjet", AC_CAT_PRIVATE}},
    {"C550", {"Cessna", "Citation II", "Cessna Citation II", "Privatjet", AC_CAT_PRIVATE}},
    {"C560", {"Cessna", "Citation V", "Cessna Citation V", "Privatjet", AC_CAT_PRIVATE}},
    {"C56X", {"Cessna", "Citation Excel/XLS", "Cessna Citation Excel/XLS", "Privatjet", AC_CAT_PRIVATE}},
    {"C650", {"Cessna", "Citation III", "Cessna Citation III", "Privatjet", AC_CAT_PRIVATE}},
    {"C680", {"Cessna", "Citation Sovereign", "Cessna Citation Sovereign", "Privatjet", AC_CAT_PRIVATE}},
    {"C68A", {"Cessna", "Citation Latitude", "Cessna Citation Latitude", "Privatjet", AC_CAT_PRIVATE}},
    {"C700", {"Cessna", "Citation Longitude", "Cessna Citation Longitude", "Privatjet", AC_CAT_PRIVATE}},
    {"C72R", {"Cessna", "172RG", "Cessna 172RG Cutlass", "Viersitzer", AC_CAT_PRIVATE}},
    {"C750", {"Cessna", "Citation X", "Cessna Citation X", "Privatjet", AC_CAT_PRIVATE}},
    {"C77R", {"Cessna", "177RG", "Cessna 177RG Cardinal", "Viersitzer", AC_CAT_PRIVATE}},
    {"C82R", {"Cessna", "182RG", "Cessna 182RG Skylane", "Viersitzer", AC_CAT_PRIVATE}},
    {"C919", {"COMAC", "C919", "COMAC C919", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"CALI", {"AutoGyro", "Calidus", "AutoGyro Calidus", "Tragschrauber", AC_CAT_PRIVATE}},
    {"CH47", {"Boeing", "CH-47", "Boeing CH-47 Chinook", "Hubschrauber", AC_CAT_MILITARY}},
    {"CL30", {"Bombardier", "Challenger 300", "Bombardier Challenger 300", "Privatjet", AC_CAT_PRIVATE}},
    {"CL35", {"Bombardier", "Challenger 350", "Bombardier Challenger 350", "Privatjet", AC_CAT_PRIVATE}},
    {"CL60", {"Bombardier", "Challenger 600", "Bombardier Challenger 600", "Privatjet", AC_CAT_PRIVATE}},
    {"CN35", {"CASA", "CN-235", "CASA CN-235", "Militärflugzeug", AC_CAT_MILITARY}},
    {"CRJ1", {"Bombardier", "CRJ100", "Bombardier CRJ100", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJ2", {"Bombardier", "CRJ200", "Bombardier CRJ200", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJ7", {"Bombardier", "CRJ700", "Bombardier CRJ700", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJ9", {"Bombardier", "CRJ900", "Bombardier CRJ900", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJX", {"Bombardier", "CRJ1000", "Bombardier CRJ1000", "Regionaljet", AC_CAT_AIRLINER}},
    {"D328", {"Dornier", "328", "Dornier 328", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"DA20", {"Diamond", "DA20", "Diamond DA20 Katana", "Zweisitzer", AC_CAT_PRIVATE}},
    {"DA40", {"Diamond", "DA40", "Diamond DA40 Star", "Viersitzer", AC_CAT_PRIVATE}},
    {"DA42", {"Diamond", "DA42", "Diamond DA42 Twin Star", "Viersitzer", AC_CAT_PRIVATE}},
    {"DA50", {"Diamond", "DA50", "Diamond DA50", "Viersitzer", AC_CAT_PRIVATE}},
    {"DA62", {"Diamond", "DA62", "Diamond DA62", "Sechssitzer", AC_CAT_PRIVATE}},
    {"DC10", {"McDonnell Douglas", "DC-10", "McDonnell Douglas DC-10", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"DC3", {"Douglas", "DC-3", "Douglas DC-3", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"DG1T", {"DG Flugzeugbau", "DG-1000T", "DG Flugzeugbau DG-1000T", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DG40", {"DG Flugzeugbau", "DG-400", "DG Flugzeugbau DG-400", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DG80", {"DG Flugzeugbau", "DG-800", "DG Flugzeugbau DG-800", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DH82", {"De Havilland", "DH.82", "De Havilland Tiger Moth", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"DH8A", {"De Havilland Canada", "Dash 8-100", "Dash 8-100", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"DH8B", {"De Havilland Canada", "Dash 8-200", "Dash 8-200", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"DH8C", {"De Havilland Canada", "Dash 8-300", "Dash 8-300", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"DH8D", {"De Havilland Canada", "Dash 8 Q400", "Dash 8 Q400", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"DHC6", {"De Havilland Canada", "DHC-6", "De Havilland Twin Otter", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"DIMO", {"Diamond", "HK36", "Diamond HK36 Super Dimona", "Motorsegler", AC_CAT_PRIVATE}},
    {"DISC", {"Schempp-Hirth", "Discus", "Schempp-Hirth Discus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DR40", {"Robin", "DR-400", "Robin DR-400", "Viersitzer", AC_CAT_PRIVATE}},
    {"DUOD", {"Schempp-Hirth", "Duo Discus", "Schempp-Hirth Duo Discus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DV20", {"Diamond", "DV20", "Diamond DV20 Katana", "Zweisitzer", AC_CAT_PRIVATE}},
    {"E110", {"Embraer", "EMB-110", "Embraer EMB-110 Bandeirante", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"E120", {"Embraer", "EMB-120", "Embraer EMB-120 Brasilia", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"E135", {"Embraer", "ERJ-135", "Embraer ERJ-135", "Regionaljet", AC_CAT_AIRLINER}},
    {"E145", {"Embraer", "ERJ-145", "Embraer ERJ-145", "Regionaljet", AC_CAT_AIRLINER}},
    {"E170", {"Embraer", "E170", "Embraer E170", "Regionaljet", AC_CAT_AIRLINER}},
    {"E175", {"Embraer", "E175", "Embraer E175", "Regionaljet", AC_CAT_AIRLINER}},
    {"E190", {"Embraer", "E190", "Embraer E190", "Regionaljet", AC_CAT_AIRLINER}},
    {"E195", {"Embraer", "E195", "Embraer E195", "Regionaljet", AC_CAT_AIRLINER}},
    {"E290", {"Embraer", "E190-E2", "Embraer E190-E2", "Regionaljet", AC_CAT_AIRLINER}},
    {"E295", {"Embraer", "E195-E2", "Embraer E195-E2", "Regionaljet", AC_CAT_AIRLINER}},
    {"E300", {"Extra", "EA-300", "Extra EA-300", "Kunstflugzeug", AC_CAT_PRIVATE}},
    {"E35L", {"Embraer", "Legacy 600", "Embraer Legacy 600", "Privatjet", AC_CAT_PRIVATE}},
    {"E3TF", {"Boeing", "E-3 Sentry", "Boeing E-3 Sentry (AWACS)", "Militärflugzeug", AC_CAT_MILITARY}},
    {"E50P", {"Embraer", "Phenom 100", "Embraer Phenom 100", "Privatjet", AC_CAT_PRIVATE}},
    {"E55P", {"Embraer", "Phenom 300", "Embraer Phenom 300", "Privatjet", AC_CAT_PRIVATE}},
    {"E75L", {"Embraer", "E175", "Embraer E175", "Regionaljet", AC_CAT_AIRLINER}},
    {"E75S", {"Embraer", "E175", "Embraer E175", "Regionaljet", AC_CAT_AIRLINER}},
    {"EA50", {"Eclipse Aerospace", "Eclipse 500", "Eclipse 500", "Privatjet", AC_CAT_PRIVATE}},
    {"EC20", {"Airbus Helicopters", "EC120", "Airbus Helicopters EC120 Colibri", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC25", {"Airbus Helicopters", "H225", "Airbus H225", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC30", {"Airbus Helicopters", "H130", "Airbus H130", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC35", {"Airbus Helicopters", "H135", "Airbus H135", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC45", {"Airbus Helicopters", "H145", "Airbus H145", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC55", {"Airbus Helicopters", "H155", "Airbus H155", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC75", {"Airbus Helicopters", "H175", "Airbus H175", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EH10", {"Leonardo", "AW101", "Leonardo AW101", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EN28", {"Enstrom", "280", "Enstrom 280", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EN48", {"Enstrom", "480", "Enstrom 480", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EUFI", {"Eurofighter", "Typhoon", "Eurofighter Typhoon", "Militärflugzeug", AC_CAT_MILITARY}},
    {"EV97", {"Evektor", "EuroStar", "Evektor EuroStar", "Ultraleichtflugzeug", AC_CAT_PRIVATE}},
    {"F100", {"Fokker", "F100", "Fokker 100", "Regionaljet", AC_CAT_AIRLINER}},
    {"F15", {"Boeing", "F-15", "Boeing F-15 Eagle", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F16", {"General Dynamics", "F-16", "General Dynamics F-16 Fighting Falcon", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F18", {"Boeing", "F/A-18", "Boeing F/A-18 Hornet", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F260", {"SIAI-Marchetti", "SF.260", "SIAI-Marchetti SF.260", "Zweisitzer", AC_CAT_PRIVATE}},
    {"F27", {"Fokker", "F27", "Fokker F27 Friendship", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"F28", {"Fokker", "F28", "Fokker F28 Fellowship", "Regionaljet", AC_CAT_AIRLINER}},
    {"F2TH", {"Dassault", "Falcon 2000", "Dassault Falcon 2000", "Privatjet", AC_CAT_PRIVATE}},
    {"F35", {"Lockheed Martin", "F-35", "Lockheed Martin F-35 Lightning II", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F5", {"Northrop", "F-5", "Northrop F-5", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F70", {"Fokker", "F70", "Fokker 70", "Regionaljet", AC_CAT_AIRLINER}},
    {"F900", {"Dassault", "Falcon 900", "Dassault Falcon 900", "Privatjet", AC_CAT_PRIVATE}},
    {"FA20", {"Dassault", "Falcon 20", "Dassault Falcon 20", "Privatjet", AC_CAT_PRIVATE}},
    {"FA50", {"Dassault", "Falcon 50", "Dassault Falcon 50", "Privatjet", AC_CAT_PRIVATE}},
    {"FA7X", {"Dassault", "Falcon 7X", "Dassault Falcon 7X", "Privatjet", AC_CAT_PRIVATE}},
    {"FA8X", {"Dassault", "Falcon 8X", "Dassault Falcon 8X", "Privatjet", AC_CAT_PRIVATE}},
    {"FK9", {"FK-Lightplanes", "FK9", "FK-Lightplanes FK9", "Ultraleichtflugzeug", AC_CAT_PRIVATE}},
    {"G280", {"Gulfstream", "G280", "Gulfstream G280", "Privatjet", AC_CAT_PRIVATE}},
    {"G2CA", {"Guimbal", "Cabri G2", "Guimbal Cabri G2", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"GA8", {"GippsAero", "GA8", "GippsAero GA8 Airvan", "Sechssitzer", AC_CAT_PRIVATE}},
    {"GAZL", {"Aérospatiale", "Gazelle", "Aérospatiale Gazelle", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"GL5T", {"Bombardier", "Global 5000", "Bombardier Global 5000", "Privatjet", AC_CAT_PRIVATE}},
    {"GL7T", {"Bombardier", "Global 7500", "Bombardier Global 7500", "Privatjet", AC_CAT_PRIVATE}},
    {"GLEX", {"Bombardier", "Global Express", "Bombardier Global Express", "Privatjet", AC_CAT_PRIVATE}},
    {"GLF2", {"Gulfstream", "Gulfstream II", "Gulfstream II", "Privatjet", AC_CAT_PRIVATE}},
    {"GLF3", {"Gulfstream", "Gulfstream III", "Gulfstream III", "Privatjet", AC_CAT_PRIVATE}},
    {"GLF4", {"Gulfstream", "Gulfstream IV", "Gulfstream IV", "Privatjet", AC_CAT_PRIVATE}},
    {"GLF5", {"Gulfstream", "Gulfstream V", "Gulfstream V", "Privatjet", AC_CAT_PRIVATE}},
    {"GLF6", {"Gulfstream", "G650", "Gulfstream G650", "Privatjet", AC_CAT_PRIVATE}},
    {"GLID", {"Segelflugzeug", "Segelflugzeug", "Segelflugzeug", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"GYRO", {"Tragschrauber", "Tragschrauber", "Tragschrauber", "Tragschrauber", AC_CAT_PRIVATE}},
    {"H125", {"Airbus Helicopters", "H125", "Airbus H125", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H130", {"Airbus Helicopters", "H130", "Airbus H130", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H135", {"Airbus Helicopters", "H135", "Airbus H135", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H145", {"Airbus Helicopters", "H145", "Airbus H145", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H160", {"Airbus Helicopters", "H160", "Airbus H160", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H175", {"Airbus Helicopters", "H175", "Airbus H175", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H25B", {"Hawker Beechcraft", "Hawker 800", "Hawker 800", "Privatjet", AC_CAT_PRIVATE}},
    {"H269", {"Schweizer", "269", "Schweizer 269", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H500", {"MD Helicopters", "MD 500", "MD Helicopters MD 500", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H60", {"Sikorsky", "UH-60", "Sikorsky UH-60 Black Hawk", "Hubschrauber", AC_CAT_MILITARY}},
    {"HAWK", {"BAE Systems", "Hawk", "BAE Systems Hawk", "Militärflugzeug", AC_CAT_MILITARY}},
    {"HDJT", {"Honda Aircraft", "HA-420", "HondaJet HA-420", "Privatjet", AC_CAT_PRIVATE}},
    {"HR20", {"Robin", "HR200", "Robin HR200", "Zweisitzer", AC_CAT_PRIVATE}},
    {"IL76", {"Ilyushin", "Il-76", "Ilyushin Il-76", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"J328", {"Dornier", "328JET", "Dornier 328JET", "Regionaljet", AC_CAT_AIRLINER}},
    {"JANU", {"Schempp-Hirth", "Janus", "Schempp-Hirth Janus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"JU52", {"Junkers", "Ju 52", "Junkers Ju 52", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"K35R", {"Boeing", "KC-135R", "Boeing KC-135R Stratotanker", "Militärflugzeug", AC_CAT_MILITARY}},
    {"L13", {"LET", "L-13", "LET L-13 Blanik", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"L410", {"LET", "L-410", "LET L-410 Turbolet", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"LJ31", {"Learjet", "Learjet 31", "Learjet 31", "Privatjet", AC_CAT_PRIVATE}},
    {"LJ35", {"Learjet", "Learjet 35", "Learjet 35", "Privatjet", AC_CAT_PRIVATE}},
    {"LJ40", {"Learjet", "Learjet 40", "Learjet 40", "Privatjet", AC_CAT_PRIVATE}},
    {"LJ45", {"Learjet", "Learjet 45", "Learjet 45", "Privatjet", AC_CAT_PRIVATE}},
    {"LJ60", {"Learjet", "Learjet 60", "Learjet 60", "Privatjet", AC_CAT_PRIVATE}},
    {"LJ75", {"Learjet", "Learjet 75", "Learjet 75", "Privatjet", AC_CAT_PRIVATE}},
    {"LS4", {"Rolladen-Schneider", "LS4", "Rolladen-Schneider LS4", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"LS6", {"Rolladen-Schneider", "LS6", "Rolladen-Schneider LS6", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"LS8", {"Rolladen-Schneider", "LS8", "Rolladen-Schneider LS8", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"M20P", {"Mooney", "M20", "Mooney M20", "Viersitzer", AC_CAT_PRIVATE}},
    {"M20T", {"Mooney", "M20", "Mooney M20", "Viersitzer", AC_CAT_PRIVATE}},
    {"M46P", {"Piper", "PA-46", "Piper Malibu/Meridian", "Viersitzer", AC_CAT_PRIVATE}},
    {"MA60", {"Xi'an", "MA60", "Xi'an MA60", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"MD11", {"McDonnell Douglas", "MD-11", "McDonnell Douglas MD-11", "Großraumflugzeug", AC_CAT_AIRLINER}},
    {"MD52", {"MD Helicopters", "MD 520N", "MD Helicopters MD 520N", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"MD82", {"McDonnell Douglas", "MD-82", "McDonnell Douglas MD-82", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"MD83", {"McDonnell Douglas", "MD-83", "McDonnell Douglas MD-83", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"MD88", {"McDonnell Douglas", "MD-88", "McDonnell Douglas MD-88", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"MD90", {"McDonnell Douglas", "MD-90", "McDonnell Douglas MD-90", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"MG29", {"Mikoyan", "MiG-29", "Mikoyan MiG-29", "Militärflugzeug", AC_CAT_MILITARY}},
    {"MI24", {"Mil", "Mi-24", "Mil Mi-24", "Hubschrauber", AC_CAT_MILITARY}},
    {"MI8", {"Mil", "Mi-8", "Mil Mi-8", "Hubschrauber", AC_CAT_MILITARY}},
    {"MTOS", {"AutoGyro", "MTOsport", "AutoGyro MTOsport", "Tragschrauber", AC_CAT_PRIVATE}},
    {"MU2", {"Mitsubishi", "MU-2", "Mitsubishi MU-2", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"NH90", {"NHIndustries", "NH90", "NHIndustries NH90", "Hubschrauber", AC_CAT_MILITARY}},
    {"NIMB", {"Schempp-Hirth", "Nimbus", "Schempp-Hirth Nimbus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"P06T", {"Tecnam", "P2006T", "Tecnam P2006T", "Viersitzer", AC_CAT_PRIVATE}},
    {"P180", {"Piaggio", "Avanti", "Piaggio P.180 Avanti", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"P208", {"Tecnam", "P2008", "Tecnam P2008", "Zweisitzer", AC_CAT_PRIVATE}},
    {"P28A", {"Piper", "PA-28", "Piper PA-28 Archer", "Viersitzer", AC_CAT_PRIVATE}},
    {"P28B", {"Piper", "PA-28", "Piper PA-28 Cherokee", "Viersitzer", AC_CAT_PRIVATE}},
    {"P28R", {"Piper", "PA-28R", "Piper PA-28R Arrow", "Viersitzer", AC_CAT_PRIVATE}},
    {"P28T", {"Piper", "PA-28R", "Piper PA-28R Turbo Arrow", "Viersitzer", AC_CAT_PRIVATE}},
    {"P3", {"Lockheed", "P-3", "Lockheed P-3 Orion", "Militärflugzeug", AC_CAT_MILITARY}},
    {"P32R", {"Piper", "PA-32R", "Piper PA-32R Saratoga", "Sechssitzer", AC_CAT_PRIVATE}},
    {"P32T", {"Piper", "PA-32RT", "Piper PA-32 Turbo Lance", "Sechssitzer", AC_CAT_PRIVATE}},
    {"P46T", {"Piper", "PA-46", "Piper PA-46 Meridian", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"P51", {"North American", "P-51", "North American P-51 Mustang", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"P68", {"Vulcanair", "P68", "Vulcanair (Partenavia) P68", "Sechssitzer", AC_CAT_PRIVATE}},
    {"P8", {"Boeing", "P-8", "Boeing P-8 Poseidon", "Militärflugzeug", AC_CAT_MILITARY}},
    {"PA18", {"Piper", "PA-18", "Piper PA-18 Super Cub", "Zweisitzer", AC_CAT_PRIVATE}},
    {"PA20", {"Piper", "PA-20", "Piper PA-20 Pacer", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA22", {"Piper", "PA-22", "Piper PA-22 Tri-Pacer", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA23", {"Piper", "PA-23", "Piper PA-23 Aztec", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA24", {"Piper", "PA-24", "Piper PA-24 Comanche", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA25", {"Piper", "PA-25", "Piper PA-25 Pawnee", "Agrarflugzeug", AC_CAT_PRIVATE}},
    {"PA28", {"Piper", "PA-28", "Piper PA-28 Cherokee", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA30", {"Piper", "PA-30", "Piper PA-30 Twin Comanche", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA31", {"Piper", "PA-31", "Piper PA-31 Navajo", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA32", {"Piper", "PA-32", "Piper PA-32 Saratoga", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA34", {"Piper", "PA-34", "Piper PA-34 Seneca", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA38", {"Piper", "PA-38", "Piper PA-38 Tomahawk", "Zweisitzer", AC_CAT_PRIVATE}},
    {"PA44", {"Piper", "PA-44", "Piper PA-44 Seminole", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA46", {"Piper", "PA-46", "Piper PA-46 Malibu", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PAY1", {"Piper", "PA-31T", "Piper PA-31T Cheyenne I", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"PAY2", {"Piper", "PA-31T", "Piper PA-31T Cheyenne", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"PAY3", {"Piper", "PA-42", "Piper PA-42 Cheyenne III", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"PC12", {"Pilatus", "PC-12", "Pilatus PC-12", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"PC21", {"Pilatus", "PC-21", "Pilatus PC-21", "Militärflugzeug", AC_CAT_MILITARY}},
    {"PC24", {"Pilatus", "PC-24", "Pilatus PC-24", "Privatjet", AC_CAT_PRIVATE}},
    {"PC6", {"Pilatus", "PC-6", "Pilatus PC-6 Porter", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"PC6T", {"Pilatus", "PC-6", "Pilatus PC-6 Turbo Porter", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"PC7", {"Pilatus", "PC-7", "Pilatus PC-7", "Militärflugzeug", AC_CAT_MILITARY}},
    {"PC9", {"Pilatus", "PC-9", "Pilatus PC-9", "Militärflugzeug", AC_CAT_MILITARY}},
    {"PITT", {"Pitts", "Special", "Pitts Special", "Kunstflugzeug", AC_CAT_PRIVATE}},
    {"PRM1", {"Beechcraft", "Premier I", "Beechcraft Premier I", "Privatjet", AC_CAT_PRIVATE}},
    {"R22", {"Robinson", "R22", "Robinson R22", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"R44", {"Robinson", "R44", "Robinson R44", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"R66", {"Robinson", "R66", "Robinson R66", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"RALL", {"Socata", "Rallye", "Socata Rallye", "Viersitzer", AC_CAT_PRIVATE}},
    {"RFAL", {"Dassault", "Rafale", "Dassault Rafale", "Militärflugzeug", AC_CAT_MILITARY}},
    {"RJ1H", {"Avro", "RJ100", "Avro RJ100", "Regionaljet", AC_CAT_AIRLINER}},
    {"RJ85", {"Avro", "RJ85", "Avro RJ85", "Regionaljet", AC_CAT_AIRLINER}},
    {"RV10", {"Van's Aircraft", "RV-10", "Van's RV-10", "Viersitzer", AC_CAT_PRIVATE}},
    {"RV12", {"Van's Aircraft", "RV-12", "Van's RV-12", "Zweisitzer", AC_CAT_PRIVATE}},
    {"RV14", {"Van's Aircraft", "RV-14", "Van's RV-14", "Zweisitzer", AC_CAT_PRIVATE}},
    {"RV4", {"Van's Aircraft", "RV-4", "Van's RV-4", "Zweisitzer", AC_CAT_PRIVATE}},
    {"RV6", {"Van's Aircraft", "RV-6", "Van's RV-6", "Zweisitzer", AC_CAT_PRIVATE}},
    {"RV7", {"Van's Aircraft", "RV-7", "Van's RV-7", "Zweisitzer", AC_CAT_PRIVATE}},
    {"RV8", {"Van's Aircraft", "RV-8", "Van's RV-8", "Zweisitzer", AC_CAT_PRIVATE}},
    {"RV9", {"Van's Aircraft", "RV-9", "Van's RV-9", "Zweisitzer", AC_CAT_PRIVATE}},
    {"S22T", {"Cirrus", "SR22T", "Cirrus SR22T", "Viersitzer", AC_CAT_PRIVATE}},
    {"S61", {"Sikorsky", "S-61", "Sikorsky S-61", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"S70", {"Sikorsky", "S-70", "Sikorsky S-70 Black Hawk", "Hubschrauber", AC_CAT_MILITARY}},
    {"S76", {"Sikorsky", "S-76", "Sikorsky S-76", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"S92", {"Sikorsky", "S-92", "Sikorsky S-92", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"SB20", {"Saab", "2000", "Saab 2000", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"SF25", {"Scheibe", "SF-25", "Scheibe SF-25 Falke", "Motorsegler", AC_CAT_PRIVATE}},
    {"SF34", {"Saab", "340", "Saab 340", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"SF50", {"Cirrus", "SF50", "Cirrus Vision Jet", "Privatjet", AC_CAT_PRIVATE}},
    {"SHIP", {"Luftschiff", "Luftschiff", "Luftschiff", "Luftschiff", AC_CAT_PRIVATE}},
    {"SNUS", {"Pipistrel", "Sinus", "Pipistrel Sinus", "Motorsegler", AC_CAT_PRIVATE}},
    {"SPIT", {"Supermarine", "Spitfire", "Supermarine Spitfire", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"SR20", {"Cirrus", "SR20", "Cirrus SR20", "Viersitzer", AC_CAT_PRIVATE}},
    {"SR22", {"Cirrus", "SR22", "Cirrus SR22", "Viersitzer", AC_CAT_PRIVATE}},
    {"STDC", {"Schempp-Hirth", "Standard Cirrus", "Schempp-Hirth Standard Cirrus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"SU26", {"Sukhoi", "Su-26", "Sukhoi Su-26", "Kunstflugzeug", AC_CAT_PRIVATE}},
    {"SU27", {"Sukhoi", "Su-27", "Sukhoi Su-27", "Militärflugzeug", AC_CAT_MILITARY}},
    {"SU95", {"Sukhoi", "Superjet 100", "Sukhoi Superjet 100", "Regionaljet", AC_CAT_AIRLINER}},
    {"SW4", {"Swearingen", "Metro", "Swearingen Metro", "Propellerflugzeug", AC_CAT_AIRLINER}},
    {"T6", {"North American", "T-6", "North American T-6 Texan", "Historisches Flugzeug", AC_CAT_PRIVATE}},
    {"TB10", {"Socata", "TB-10", "Socata TB-10 Tobago", "Viersitzer", AC_CAT_PRIVATE}},
    {"TB20", {"Socata", "TB-20", "Socata TB-20 Trinidad", "Viersitzer", AC_CAT_PRIVATE}},
    {"TBM7", {"Socata", "TBM 700", "Socata TBM 700", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"TBM8", {"Daher-Socata", "TBM 850", "Daher-Socata TBM 850", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"TBM9", {"Daher", "TBM 900", "Daher TBM 900", "Propellerflugzeug", AC_CAT_PRIVATE}},
    {"TEX2", {"Beechcraft", "T-6", "Beechcraft T-6 Texan II", "Militärflugzeug", AC_CAT_MILITARY}},
    {"TIGR", {"Airbus Helicopters", "Tiger", "Airbus Tiger", "Hubschrauber", AC_CAT_MILITARY}},
    {"TORN", {"Panavia", "Tornado", "Panavia Tornado", "Militärflugzeug", AC_CAT_MILITARY}},
    /* A fixed ground structure seen by ADS-B receivers — a tower, a mast, an
     * oil platform — not an aircraft and not an MLAT artefact, which is what
     * the previous text ("Boden-Referenzsignal (MLAT)") claimed. It also could
     * never be reached: manufacturer "-" makes actype_is_placeholder() true,
     * so actype_display_name() skipped the row entirely and the panel said
     * "Unbekanntes Flugzeug" at him for a radio mast. The four fields repeat
     * the word for the same reason the class designators do.
     *
     * "Bodenstation" is the term FlightAware's and AirNav's German pages use.
     * The size_class line says plainly what matters most: it is not a plane. */
    {"TWR", {"Bodenstation", "Bodenstation", "Bodenstation", "Kein Flugzeug", AC_CAT_UNKNOWN}},
    {"UH1", {"Bell", "UH-1", "Bell UH-1 Huey", "Hubschrauber", AC_CAT_MILITARY}},
    {"UH60", {"Sikorsky", "UH-60", "Sikorsky UH-60 Black Hawk", "Hubschrauber", AC_CAT_MILITARY}},
    {"ULAC", {"Ultraleichtflugzeug", "Ultraleichtflugzeug", "Ultraleichtflugzeug", "Ultraleichtflugzeug", AC_CAT_PRIVATE}},
    {"VENT", {"Schempp-Hirth", "Ventus", "Schempp-Hirth Ventus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"VIRU", {"Pipistrel", "Virus", "Pipistrel Virus", "Ultraleichtflugzeug", AC_CAT_PRIVATE}},
    {"VL3", {"JMB Aircraft", "VL-3", "JMB VL-3", "Ultraleichtflugzeug", AC_CAT_PRIVATE}},
    {"WILG", {"PZL", "PZL-104", "PZL-104 Wilga", "Viersitzer", AC_CAT_PRIVATE}},
    {"WT9", {"Aerospool", "WT9", "Aerospool WT9 Dynamic", "Ultraleichtflugzeug", AC_CAT_PRIVATE}},
    {"YK18", {"Yakovlev", "Yak-18T", "Yakovlev Yak-18T", "Viersitzer", AC_CAT_PRIVATE}},
    {"YK52", {"Yakovlev", "Yak-52", "Yakovlev Yak-52", "Kunstflugzeug", AC_CAT_PRIVATE}},
    {"Z242", {"Zlin", "Z 242", "Zlin 242", "Zweisitzer", AC_CAT_PRIVATE}},
    {"Z43", {"Zlin", "Z 43", "Zlin 43", "Viersitzer", AC_CAT_PRIVATE}},
};

#define ACTYPE_COUNT (sizeof(ACTYPES) / sizeof(ACTYPES[0]))

static int actype_cmp(const void *key, const void *elem)
{
    const char *k = (const char *)key;
    const actype_lookup_t *e = (const actype_lookup_t *)elem;
    return strcmp(k, e->icao_type);
}

const ac_type_t *actype(const char *icao_type)
{
    if (icao_type == NULL || icao_type[0] == '\0') {
        return NULL;
    }
    const actype_lookup_t *hit = (const actype_lookup_t *)bsearch(
        icao_type, ACTYPES, ACTYPE_COUNT, sizeof(ACTYPES[0]), actype_cmp);
    return hit ? &hit->info : NULL;
}

const char *actype_full_or_code(const char *icao_type)
{
    /* "Never NULL, never empty" is absolute: the panel must always show
     * something. A NULL/empty code is not a valid ICAO type at all, so it
     * cannot fall back to "the raw code" -- use a visible placeholder
     * instead of risking an empty hero line. */
    if (icao_type == NULL || icao_type[0] == '\0') {
        return "?";
    }
    const ac_type_t *t = actype(icao_type);
    if (t != NULL && t->full_name != NULL && t->full_name[0] != '\0') {
        return t->full_name;
    }
    return icao_type;
}

const actype_lookup_t *tbl_actype_entries(size_t *count)
{
    *count = ACTYPE_COUNT;
    return ACTYPES;
}


/* ---------------------------------------------------------------------------
 * ICAO emitter category -> plain German class.
 *
 * Used only when the type designator is absent. In the 2026-09-18 capture over
 * Gloggnitz, OEVSO and OEANW were real aircraft (category A1, 160 kt and 87 kt)
 * with no `t` and no `r` — the hero rendered as "?" before this existed.
 * Categories per ICAO Doc 9871 / DO-260B.
 * ------------------------------------------------------------------------- */
/* Read against DO-260B Table 2-21 directly, not against a paraphrase. Two
 * things about the standard drive the wording:
 *
 *   The A-set is a WAKE VORTEX declaration, not a size statement. DO-260B's
 *   own note says the codes advise others of wake characteristics "and not
 *   necessarily the transmitting aircraft's actual maximum take-off weight",
 *   and that in doubt the next HIGHER code should be used. So a word that
 *   asserts a fuselage shape or a role is betting on something the broadcast
 *   does not claim.
 *
 *   A1-A5's boundaries are exactly ICAO's wake classes (7 t / 136 t), so
 *   A2, A3 and A4 all sit inside German "Mittel". There is no German term for
 *   the A2/A3 split because German does not make it.
 *
 * There is no published German translation of this table anywhere — EASA,
 * LBA, DFS and Austro Control all lack one — so every word here is a
 * judgement anchored on the German and Austrian term for the THING. */
static const str_lookup_t k_category_de[] = {
    /* A1 and A2 were "Leichtflugzeug" and "Kleinflugzeug", which German uses
     * as SYNONYMS for the same ~5.7 t class — so the table spent a synonym
     * pair on two different weight bands, and the larger band got the word
     * that sounds smaller. A2 reaches 34 t: a Dash 8 Q400 or an ATR 72 is not
     * a "Kleinflugzeug" by any German definition. */
    {"A1", "Kleinflugzeug"},          /* <7 t — the word Austrian press uses */
    {"A2", "Mittelgroßes Flugzeug"},  /* 7-34 t; a description, since no term exists */
    {"A3", "Verkehrsflugzeug"},       /* 34-136 t */
    /* A4 is "High-Vortex Large", and DO-260B names the B-757 as its example —
     * a NARROWBODY. "Großraumflugzeug" means widebody specifically (>5 m
     * fuselage, two aisles), so it asserted the one thing A4 is known not to
     * be. A4 sits inside A3's weight band and adds only a wake warning that is
     * invisible to him, so it gets A3's word. */
    {"A4", "Verkehrsflugzeug"},
    {"A5", "Großraumflugzeug"},       /* >136 t; true of every civil airliner that heavy */
    /* A6 was "Hochleistungsflugzeug", which is a real EASA Part-FCL term
     * (High Performance Airplane) meaning a single-pilot TBM, King Air or
     * Citation — roughly the OPPOSITE of what A6 is. DO-260B A6 is ">5g and
     * >400 knots", which in practice is only ever a fast military jet. */
    {"A6", "Militärjet"},
    {"A7", "Hubschrauber"},           /* Rotorcraft; narrows Tragschrauber, acceptable */

    {"B1", "Segelflugzeug"},
    {"B2", "Ballon"},                 /* narrows "Lighter-than-Air"; see the BALL row */
    {"B3", "Fallschirmspringer"},
    {"B4", "Ultraleichtflugzeug"},    /* narrows; hang-gliders and Paragleiter fly FLARM */
    {"B6", "Drohne"},
    {"B7", "Raumfahrzeug"},

    /* Set C was missing entirely, and it is the set that matters most here:
     * these are SURFACE vehicles and fixed obstacles. With no entry,
     * ac_category_de() returned NULL and the panel announced a fire truck to
     * him as "Unbekanntes Flugzeug" in 76 px. C0 is the sharp one — it means
     * "I am a Set C emitter and not saying which", NOT "unknown aircraft".
     *
     * The German is Austro Control's own, from its Datenproduktspezifikation
     * für Luftfahrthindernisse: Punktobjekte (Mast, Antenne) -> C3,
     * Linienobjekte (Hochspannungsleitung, Seilbahn) -> C5, and
     * "Hindernisgruppe" is that document's word for a cluster. C3 and C5 are
     * collapsed because Punkt-versus-Linie is a surveying distinction with no
     * meaning to him, and "fest" stays true for a Fesselballon, which is
     * tethered but airborne. The load-bearing property of every word below is
     * that none of them contains "Flugzeug". */
    {"C0", "Fahrzeug oder Hindernis"},
    {"C1", "Einsatzfahrzeug"},
    {"C2", "Flughafenfahrzeug"},
    {"C3", "Festes Hindernis"},
    {"C4", "Hindernisgruppe"},
    {"C5", "Festes Hindernis"},
};

const char *ac_category_de(const char *icao_category)
{
    if (icao_category == NULL || icao_category[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < sizeof k_category_de / sizeof k_category_de[0]; i++) {
        if (strcmp(k_category_de[i].key, icao_category) == 0) {
            return k_category_de[i].value;
        }
    }
    return NULL;
}


/* True when the table admits it could not identify the aircraft: its
 * manufacturer is a placeholder, so its "model" is just the ICAO code again.
 * Note the near-miss that makes the MANUFACTURER the right signal rather than
 * the model — Diamond's aircraft really is called "DV20". */
static bool actype_is_placeholder(const ac_type_t *t)
{
    if (t == NULL || t->manufacturer == NULL) {
        return true;
    }
    return t->manufacturer[0] == '\0' ||
           strcmp(t->manufacturer, "unbekannt") == 0 ||
           strcmp(t->manufacturer, "-") == 0;
}

const char *actype_display_name(const char *icao_type, const char *icao_category)
{
    const ac_type_t *t = actype(icao_type);
    if (t != NULL && !actype_is_placeholder(t)) {
        if (t->full_name != NULL && t->full_name[0] != '\0') return t->full_name;
        if (t->model     != NULL && t->model[0]     != '\0') return t->model;
    }
    return ac_category_de(icao_category);   /* NULL when that is unknown too */
}

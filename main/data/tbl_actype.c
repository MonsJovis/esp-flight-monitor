/* ICAO aircraft-type designator -> structured entry, keyed on the `t` field
 * from adsb.lol / adsb.fi (e.g. "A20N", "DV20", "EC35").
 *
 * `category` (see ac_type_t in flight_types.h) drives DESIGN.md §5.2: it is
 * what lets the "Ohne Route" screen say *why* an aircraft has no route
 * instead of leaving a blank slot. AC_CAT_PRIVATE means "no route is normal",
 * not an error -- getting the airliner/private split right matters more than
 * the exact wording of any one entry.
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

static const actype_lookup_t ACTYPES[] = {
    {"A109", {"Leonardo", "A109", "Leonardo (Agusta) A109", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A124", {"Antonov", "An-124", "Antonov An-124 Ruslan", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"A139", {"Leonardo", "AW139", "Leonardo AW139", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"A19N", {"Airbus", "A319neo", "Airbus A319neo", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A20N", {"Airbus", "A320neo", "Airbus A320neo", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A21N", {"Airbus", "A321neo", "Airbus A321neo", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A306", {"Airbus", "A300-600", "Airbus A300-600", "Großraumjet", AC_CAT_AIRLINER}},
    {"A310", {"Airbus", "A310", "Airbus A310", "Großraumjet", AC_CAT_AIRLINER}},
    {"A318", {"Airbus", "A318", "Airbus A318", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A319", {"Airbus", "A319", "Airbus A319", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A320", {"Airbus", "A320", "Airbus A320", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A321", {"Airbus", "A321", "Airbus A321", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"A332", {"Airbus", "A330-200", "Airbus A330-200", "Großraumjet", AC_CAT_AIRLINER}},
    {"A333", {"Airbus", "A330-300", "Airbus A330-300", "Großraumjet", AC_CAT_AIRLINER}},
    {"A339", {"Airbus", "A330-900", "Airbus A330-900neo", "Großraumjet", AC_CAT_AIRLINER}},
    {"A343", {"Airbus", "A340-300", "Airbus A340-300", "Großraumjet", AC_CAT_AIRLINER}},
    {"A346", {"Airbus", "A340-600", "Airbus A340-600", "Großraumjet", AC_CAT_AIRLINER}},
    {"A359", {"Airbus", "A350-900", "Airbus A350-900", "Großraumjet", AC_CAT_AIRLINER}},
    {"A35K", {"Airbus", "A350-1000", "Airbus A350-1000", "Großraumjet", AC_CAT_AIRLINER}},
    {"A388", {"Airbus", "A380-800", "Airbus A380", "Großraumjet", AC_CAT_AIRLINER}},
    {"A400", {"Airbus", "A400M", "Airbus A400M Atlas", "Militärflugzeug", AC_CAT_MILITARY}},
    {"AH64", {"Boeing", "AH-64", "Boeing AH-64 Apache", "Hubschrauber", AC_CAT_MILITARY}},
    {"AQUI", {"Aquila", "AT01", "Aquila AT01", "Zweisitzer", AC_CAT_PRIVATE}},
    {"ARCU", {"Schempp-Hirth", "Arcus", "Schempp-Hirth Arcus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"AS50", {"Airbus Helicopters", "AS350", "Airbus Helicopters AS350 Ecureuil", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"AS55", {"Airbus Helicopters", "AS355", "Airbus Helicopters AS355 TwinStar", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"AT43", {"ATR", "42-300", "ATR 42-300", "Turboprop", AC_CAT_AIRLINER}},
    {"AT45", {"ATR", "42-500", "ATR 42-500", "Turboprop", AC_CAT_AIRLINER}},
    {"AT72", {"ATR", "72", "ATR 72", "Turboprop", AC_CAT_AIRLINER}},
    {"AW09", {"Leonardo", "AW009", "Leonardo AW009", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B06", {"Bell", "206", "Bell 206 JetRanger", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B190", {"Beechcraft", "1900D", "Beechcraft 1900D", "Turboprop", AC_CAT_AIRLINER}},
    {"B212", {"Bell", "212", "Bell 212", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B350", {"Beechcraft", "King Air 350", "Beechcraft King Air 350", "Turboprop", AC_CAT_PRIVATE}},
    {"B37M", {"Boeing", "737 MAX 7", "Boeing 737 MAX 7", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B38M", {"Boeing", "737 MAX 8", "Boeing 737 MAX 8", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B39M", {"Boeing", "737 MAX 9", "Boeing 737 MAX 9", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B3XM", {"Boeing", "737 MAX 10", "Boeing 737 MAX 10", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B412", {"Bell", "412", "Bell 412", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B429", {"Bell", "429", "Bell 429", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"B461", {"British Aerospace", "BAe 146-100", "BAe 146-100", "Regionaljet", AC_CAT_AIRLINER}},
    {"B462", {"British Aerospace", "BAe 146-200", "BAe 146-200", "Regionaljet", AC_CAT_AIRLINER}},
    {"B463", {"British Aerospace", "BAe 146-300", "BAe 146-300", "Regionaljet", AC_CAT_AIRLINER}},
    {"B722", {"Boeing", "727-200", "Boeing 727-200", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B735", {"Boeing", "737-500", "Boeing 737-500", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B736", {"Boeing", "737-600", "Boeing 737-600", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B737", {"Boeing", "737-700", "Boeing 737-700", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B738", {"Boeing", "737-800", "Boeing 737-800", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B739", {"Boeing", "737-900", "Boeing 737-900", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B744", {"Boeing", "747-400", "Boeing 747-400", "Großraumjet", AC_CAT_AIRLINER}},
    {"B748", {"Boeing", "747-8", "Boeing 747-8", "Großraumjet", AC_CAT_AIRLINER}},
    {"B752", {"Boeing", "757-200", "Boeing 757-200", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B753", {"Boeing", "757-300", "Boeing 757-300", "Mittelstreckenjet", AC_CAT_AIRLINER}},
    {"B762", {"Boeing", "767-200", "Boeing 767-200", "Großraumjet", AC_CAT_AIRLINER}},
    {"B763", {"Boeing", "767-300", "Boeing 767-300", "Großraumjet", AC_CAT_AIRLINER}},
    {"B764", {"Boeing", "767-400", "Boeing 767-400", "Großraumjet", AC_CAT_AIRLINER}},
    {"B772", {"Boeing", "777-200", "Boeing 777-200", "Großraumjet", AC_CAT_AIRLINER}},
    {"B773", {"Boeing", "777-300", "Boeing 777-300", "Großraumjet", AC_CAT_AIRLINER}},
    {"B77L", {"Boeing", "777F", "Boeing 777 Freighter", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"B77W", {"Boeing", "777-300ER", "Boeing 777-300ER", "Großraumjet", AC_CAT_AIRLINER}},
    {"B788", {"Boeing", "787-8", "Boeing 787-8 Dreamliner", "Großraumjet", AC_CAT_AIRLINER}},
    {"B789", {"Boeing", "787-9", "Boeing 787-9 Dreamliner", "Großraumjet", AC_CAT_AIRLINER}},
    {"B78X", {"Boeing", "787-10", "Boeing 787-10 Dreamliner", "Großraumjet", AC_CAT_AIRLINER}},
    {"BCS1", {"Airbus", "A220-100", "Airbus A220-100", "Regionaljet", AC_CAT_AIRLINER}},
    {"BCS3", {"Airbus", "A220-300", "Airbus A220-300", "Regionaljet", AC_CAT_AIRLINER}},
    {"BE20", {"Beechcraft", "King Air 200", "Beechcraft King Air 200", "Turboprop", AC_CAT_PRIVATE}},
    {"BE36", {"Beechcraft", "Bonanza", "Beechcraft Bonanza", "Viersitzer", AC_CAT_PRIVATE}},
    {"BE40", {"Beechcraft", "Beechjet 400", "Beechcraft Beechjet 400", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"BE58", {"Beechcraft", "Baron 58", "Beechcraft Baron 58", "Sechssitzer", AC_CAT_PRIVATE}},
    {"BE9L", {"Beechcraft", "King Air 90", "Beechcraft King Air 90", "Turboprop", AC_CAT_PRIVATE}},
    {"C130", {"Lockheed", "C-130", "Lockheed C-130 Hercules", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C150", {"Cessna", "150", "Cessna 150", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C152", {"Cessna", "152", "Cessna 152", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C17", {"Boeing", "C-17", "Boeing C-17 Globemaster III", "Militärflugzeug", AC_CAT_MILITARY}},
    {"C170", {"Cessna", "170", "Cessna 170", "Viersitzer", AC_CAT_PRIVATE}},
    {"C172", {"Cessna", "172", "Cessna 172 Skyhawk", "Viersitzer", AC_CAT_PRIVATE}},
    {"C180", {"Cessna", "180", "Cessna 180 Skywagon", "Viersitzer", AC_CAT_PRIVATE}},
    {"C182", {"Cessna", "182", "Cessna 182 Skylane", "Viersitzer", AC_CAT_PRIVATE}},
    {"C185", {"Cessna", "185", "Cessna 185 Skywagon", "Viersitzer", AC_CAT_PRIVATE}},
    {"C206", {"Cessna", "206", "Cessna 206 Stationair", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C208", {"Cessna", "Caravan", "Cessna 208 Caravan", "Turboprop", AC_CAT_PRIVATE}},
    {"C210", {"Cessna", "210", "Cessna 210 Centurion", "Sechssitzer", AC_CAT_PRIVATE}},
    {"C25A", {"Cessna", "Citation CJ2", "Cessna Citation CJ2", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C25B", {"Cessna", "Citation CJ3", "Cessna Citation CJ3", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C25C", {"Cessna", "Citation CJ4", "Cessna Citation CJ4", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C42", {"Ikarus", "C42", "Ikarus C42", "Zweisitzer", AC_CAT_PRIVATE}},
    {"C500", {"Cessna", "Citation I", "Cessna Citation I", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C510", {"Cessna", "Citation Mustang", "Cessna Citation Mustang", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C525", {"Cessna", "CitationJet", "Cessna CitationJet", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C550", {"Cessna", "Citation II", "Cessna Citation II", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C560", {"Cessna", "Citation V", "Cessna Citation V", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C56X", {"Cessna", "Citation Excel/XLS", "Cessna Citation Excel/XLS", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C650", {"Cessna", "Citation III", "Cessna Citation III", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C680", {"Cessna", "Citation Sovereign", "Cessna Citation Sovereign", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"C750", {"Cessna", "Citation X", "Cessna Citation X", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"CH47", {"Boeing", "CH-47", "Boeing CH-47 Chinook", "Hubschrauber", AC_CAT_MILITARY}},
    {"CL35", {"Bombardier", "Challenger 350", "Bombardier Challenger 350", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"CL60", {"Bombardier", "Challenger 600", "Bombardier Challenger 600", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"CRJ1", {"Bombardier", "CRJ100", "Bombardier CRJ100", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJ2", {"Bombardier", "CRJ200", "Bombardier CRJ200", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJ7", {"Bombardier", "CRJ700", "Bombardier CRJ700", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJ9", {"Bombardier", "CRJ900", "Bombardier CRJ900", "Regionaljet", AC_CAT_AIRLINER}},
    {"CRJX", {"Bombardier", "CRJ1000", "Bombardier CRJ1000", "Regionaljet", AC_CAT_AIRLINER}},
    {"D328", {"Dornier", "328", "Dornier 328", "Turboprop", AC_CAT_AIRLINER}},
    {"DA40", {"Diamond", "DA40", "Diamond DA40 Star", "Viersitzer", AC_CAT_PRIVATE}},
    {"DA42", {"Diamond", "DA42", "Diamond DA42 Twin Star", "Viersitzer", AC_CAT_PRIVATE}},
    {"DC10", {"McDonnell Douglas", "DC-10", "McDonnell Douglas DC-10", "Großraumjet", AC_CAT_AIRLINER}},
    {"DG40", {"DG Flugzeugbau", "DG-400", "DG Flugzeugbau DG-400", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DH8A", {"De Havilland Canada", "Dash 8-100", "Dash 8-100", "Turboprop", AC_CAT_AIRLINER}},
    {"DH8B", {"De Havilland Canada", "Dash 8-200", "Dash 8-200", "Turboprop", AC_CAT_AIRLINER}},
    {"DH8C", {"De Havilland Canada", "Dash 8-300", "Dash 8-300", "Turboprop", AC_CAT_AIRLINER}},
    {"DH8D", {"De Havilland Canada", "Dash 8 Q400", "Dash 8 Q400", "Turboprop", AC_CAT_AIRLINER}},
    {"DUOD", {"Schempp-Hirth", "Duo Discus", "Schempp-Hirth Duo Discus", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"DV20", {"Diamond", "DV20", "Diamond DV20 Katana", "Zweisitzer", AC_CAT_PRIVATE}},
    {"E120", {"Embraer", "EMB-120", "Embraer EMB-120 Brasilia", "Turboprop", AC_CAT_AIRLINER}},
    {"E135", {"Embraer", "ERJ-135", "Embraer ERJ-135", "Regionaljet", AC_CAT_AIRLINER}},
    {"E145", {"Embraer", "ERJ-145", "Embraer ERJ-145", "Regionaljet", AC_CAT_AIRLINER}},
    {"E170", {"Embraer", "E170", "Embraer E170", "Regionaljet", AC_CAT_AIRLINER}},
    {"E175", {"Embraer", "E175", "Embraer E175", "Regionaljet", AC_CAT_AIRLINER}},
    {"E190", {"Embraer", "E190", "Embraer E190", "Regionaljet", AC_CAT_AIRLINER}},
    {"E195", {"Embraer", "E195", "Embraer E195", "Regionaljet", AC_CAT_AIRLINER}},
    {"E290", {"Embraer", "E190-E2", "Embraer E190-E2", "Regionaljet", AC_CAT_AIRLINER}},
    {"E295", {"Embraer", "E195-E2", "Embraer E195-E2", "Regionaljet", AC_CAT_AIRLINER}},
    {"E3TF", {"Boeing", "E-3 Sentry", "Boeing E-3 Sentry (AWACS)", "Militärflugzeug", AC_CAT_MILITARY}},
    {"E55P", {"Embraer", "Phenom 300", "Embraer Phenom 300", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"EA50", {"Eclipse Aerospace", "Eclipse 500", "Eclipse 500", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"EC20", {"Airbus Helicopters", "EC120", "Airbus Helicopters EC120 Colibri", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC30", {"Airbus Helicopters", "H130", "Airbus H130", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC35", {"Airbus Helicopters", "H135", "Airbus H135", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC45", {"Airbus Helicopters", "H145", "Airbus H145", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EC55", {"Airbus Helicopters", "H155", "Airbus H155", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"EUFI", {"Eurofighter", "Typhoon", "Eurofighter Typhoon", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F100", {"Fokker", "F100", "Fokker 100", "Regionaljet", AC_CAT_AIRLINER}},
    {"F16", {"General Dynamics", "F-16", "General Dynamics F-16 Fighting Falcon", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F18", {"Boeing", "F/A-18", "Boeing F/A-18 Hornet", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F2TH", {"Dassault", "Falcon 2000", "Dassault Falcon 2000", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"F35", {"Lockheed Martin", "F-35", "Lockheed Martin F-35 Lightning II", "Militärflugzeug", AC_CAT_MILITARY}},
    {"F70", {"Fokker", "F70", "Fokker 70", "Regionaljet", AC_CAT_AIRLINER}},
    {"F900", {"Dassault", "Falcon 900", "Dassault Falcon 900", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"FA20", {"Dassault", "Falcon 20", "Dassault Falcon 20", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"FA50", {"Dassault", "Falcon 50", "Dassault Falcon 50", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"FA7X", {"Dassault", "Falcon 7X", "Dassault Falcon 7X", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"G2CA", {"unbekannt", "G2CA", "Experimentalflugzeug (Typ G2CA)", "Zweisitzer", AC_CAT_PRIVATE}},
    {"GA8", {"GippsAero", "GA8", "GippsAero GA8 Airvan", "Sechssitzer", AC_CAT_PRIVATE}},
    {"GL5T", {"Bombardier", "Global 5000", "Bombardier Global 5000", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GL7T", {"Bombardier", "Global 7500", "Bombardier Global 7500", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GLEX", {"Bombardier", "Global Express", "Bombardier Global Express", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GLF2", {"Gulfstream", "Gulfstream II", "Gulfstream II", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GLF3", {"Gulfstream", "Gulfstream III", "Gulfstream III", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GLF4", {"Gulfstream", "Gulfstream IV", "Gulfstream IV", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GLF5", {"Gulfstream", "Gulfstream V", "Gulfstream V", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"GLF6", {"Gulfstream", "G650", "Gulfstream G650", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"H160", {"Airbus Helicopters", "H160", "Airbus H160", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H25B", {"Hawker Beechcraft", "Hawker 800", "Hawker 800", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"H500", {"MD Helicopters", "MD 500", "MD Helicopters MD 500", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"H60", {"Sikorsky", "UH-60", "Sikorsky UH-60 Black Hawk", "Hubschrauber", AC_CAT_MILITARY}},
    {"IL76", {"Ilyushin", "Il-76", "Ilyushin Il-76", "Frachtflugzeug", AC_CAT_AIRLINER}},
    {"J328", {"Dornier", "328JET", "Dornier 328JET", "Regionaljet", AC_CAT_AIRLINER}},
    {"K35R", {"Boeing", "KC-135R", "Boeing KC-135R Stratotanker", "Militärflugzeug", AC_CAT_MILITARY}},
    {"L410", {"LET", "L-410", "LET L-410 Turbolet", "Turboprop", AC_CAT_AIRLINER}},
    {"LJ35", {"Learjet", "Learjet 35", "Learjet 35", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"LJ40", {"Learjet", "Learjet 40", "Learjet 40", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"LJ45", {"Learjet", "Learjet 45", "Learjet 45", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"LJ60", {"Learjet", "Learjet 60", "Learjet 60", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"LS4", {"Rolladen-Schneider", "LS4", "Rolladen-Schneider LS4", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"LS8", {"Rolladen-Schneider", "LS8", "Rolladen-Schneider LS8", "Segelflugzeug", AC_CAT_PRIVATE}},
    {"M20P", {"Mooney", "M20", "Mooney M20", "Viersitzer", AC_CAT_PRIVATE}},
    {"M46P", {"Piper", "PA-46", "Piper Malibu/Meridian", "Viersitzer", AC_CAT_PRIVATE}},
    {"MD11", {"McDonnell Douglas", "MD-11", "McDonnell Douglas MD-11", "Großraumjet", AC_CAT_AIRLINER}},
    {"MI24", {"Mil", "Mi-24", "Mil Mi-24", "Hubschrauber", AC_CAT_MILITARY}},
    {"MI8", {"Mil", "Mi-8", "Mil Mi-8", "Hubschrauber", AC_CAT_MILITARY}},
    {"P180", {"Piaggio", "Avanti", "Piaggio P.180 Avanti", "Turboprop", AC_CAT_PRIVATE}},
    {"P2002", {"Tecnam", "P2002 Sierra", "Tecnam P2002 Sierra", "Zweisitzer", AC_CAT_PRIVATE}},
    {"P208", {"Tecnam", "P2008", "Tecnam P2008", "Zweisitzer", AC_CAT_PRIVATE}},
    {"P28A", {"Piper", "PA-28", "Piper PA-28 Archer", "Viersitzer", AC_CAT_PRIVATE}},
    {"P68", {"Vulcanair", "P68", "Vulcanair (Partenavia) P68", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA28", {"Piper", "PA-28", "Piper PA-28 Cherokee", "Viersitzer", AC_CAT_PRIVATE}},
    {"PA31", {"Piper", "PA-31", "Piper PA-31 Navajo", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA32", {"Piper", "PA-32", "Piper PA-32 Saratoga", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PA34", {"Piper", "PA-34", "Piper PA-34 Seneca", "Sechssitzer", AC_CAT_PRIVATE}},
    {"PAY2", {"Piper", "PA-31T", "Piper PA-31T Cheyenne", "Turboprop", AC_CAT_PRIVATE}},
    {"PC12", {"Pilatus", "PC-12", "Pilatus PC-12", "Turboprop", AC_CAT_PRIVATE}},
    {"PC21", {"Pilatus", "PC-21", "Pilatus PC-21", "Militärflugzeug", AC_CAT_MILITARY}},
    {"PC24", {"Pilatus", "PC-24", "Pilatus PC-24", "Geschäftsreisejet", AC_CAT_PRIVATE}},
    {"PC6", {"Pilatus", "PC-6", "Pilatus PC-6 Porter", "Turboprop", AC_CAT_PRIVATE}},
    {"PC7", {"Pilatus", "PC-7", "Pilatus PC-7", "Militärflugzeug", AC_CAT_MILITARY}},
    {"R22", {"Robinson", "R22", "Robinson R22", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"R44", {"Robinson", "R44", "Robinson R44", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"R66", {"Robinson", "R66", "Robinson R66", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"RJ1H", {"Avro", "RJ100", "Avro RJ100", "Regionaljet", AC_CAT_AIRLINER}},
    {"RJ85", {"Avro", "RJ85", "Avro RJ85", "Regionaljet", AC_CAT_AIRLINER}},
    {"S76", {"Sikorsky", "S-76", "Sikorsky S-76", "Hubschrauber", AC_CAT_HELICOPTER}},
    {"SB20", {"Saab", "2000", "Saab 2000", "Turboprop", AC_CAT_AIRLINER}},
    {"SF34", {"Saab", "340", "Saab 340", "Turboprop", AC_CAT_AIRLINER}},
    {"SR20", {"Cirrus", "SR20", "Cirrus SR20", "Viersitzer", AC_CAT_PRIVATE}},
    {"SR22", {"Cirrus", "SR22", "Cirrus SR22", "Viersitzer", AC_CAT_PRIVATE}},
    {"TBM7", {"Socata", "TBM 700", "Socata TBM 700", "Turboprop", AC_CAT_PRIVATE}},
    {"TBM8", {"Daher-Socata", "TBM 850", "Daher-Socata TBM 850", "Turboprop", AC_CAT_PRIVATE}},
    {"TBM9", {"Daher", "TBM 900", "Daher TBM 900", "Turboprop", AC_CAT_PRIVATE}},
    {"TORN", {"Panavia", "Tornado", "Panavia Tornado", "Militärflugzeug", AC_CAT_MILITARY}},
    {"TWR", {"-", "Bodenreferenz", "Boden-Referenzsignal (MLAT)", "Referenzsignal", AC_CAT_UNKNOWN}},
    {"UH1", {"Bell", "UH-1", "Bell UH-1 Huey", "Hubschrauber", AC_CAT_MILITARY}},
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
    const ac_type_t *t = actype(icao_type);
    if (t != NULL && t->full_name != NULL && t->full_name[0] != '\0') {
        return t->full_name;
    }
    return (icao_type != NULL) ? icao_type : "";
}

const actype_lookup_t *tbl_actype_entries(size_t *count)
{
    *count = ACTYPE_COUNT;
    return ACTYPES;
}

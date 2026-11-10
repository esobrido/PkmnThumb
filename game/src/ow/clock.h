/*
 * ow/clock.h — the in-game clock, and the palette group an hour picks.
 *
 * The original reads a battery-backed RTC (home/time.asm, engine/rtc/). The
 * Thumby Color has none, and Mote's API hands the game a frame delta and nothing
 * else, so the port accumulates the time the player actually plays. That is the
 * original's own fallback: with the RTC option off, a game second still takes a
 * real second of play, and the clock runs NO_RTC_SPEEDUP (6) game seconds ahead
 * for each one, so a whole day passes in four hours of play instead of
 * twenty-four (data/options/initial_options_descriptions.asm says exactly that).
 *
 * The hours at which the day changes are the original's: MORN starts at 5, DAY
 * at 9, EVE at 17, NITE at 21 (constants/misc_constants.asm). GetTimeOfDay maps
 * anything before MORN to NITE, which is why the night wraps around midnight
 * rather than starting at 21 and ending there.
 *
 * Pure: a struct and a couple of ints, no Mote, no globals.
 */
#ifndef OW_CLOCK_H
#define OW_CLOCK_H

#include <stdint.h>

/* The four phases, numbered the way the original numbers wTimeOfDay
 * (constants/ram_constants.asm: MORN 0, DAY 1, NITE 2, EVE 3) and the way
 * gfx/tilesets/bg_tiles.pal lists its outdoor groups. OW_TOD_INDOOR is not a
 * phase: it is the fifth palette group, used at any hour by maps that ask for it. */
enum {
    OW_TOD_MORN,
    OW_TOD_DAY,
    OW_TOD_NITE,
    OW_TOD_EVE,
    OW_TOD_COUNT,
    OW_TOD_INDOOR = OW_TOD_COUNT
};

/* The time field of a map header (`map <Name>, <tileset>, <env>, ..., PALETTE_X`
 * in data/maps/maps.asm), with the original's numbering
 * (constants/map_data_constants.asm). PALETTE_AUTO follows the clock; the rest
 * pin the map to one group. */
enum {
    OW_PAL_AUTO,
    OW_PAL_DAY,
    OW_PAL_NITE,
    OW_PAL_MORN,
    OW_PAL_EVE,
    OW_PAL_INDOOR
};

/* NO_RTC_SPEEDUP: game seconds per real second when there is no RTC. */
enum { OW_CLOCK_SPEEDUP = 6 };

/* How a map's environment and time field pick a palette group
 * (engine/gfx/color.asm LoadMapPals -> data/maps/environment_colors.asm):
 * a TOWN/ROUTE/ISOLATED/CAVE/DUNGEON map uses the phase's outdoor group, and an
 * INDOOR/GATE map uses the interior group in the morning and by day but the
 * outdoor nite/eve groups at those hours — which is why a house goes blue at
 * night. A map pinned to PALETTE_DAY (every interior in this build) never does. */

/* The hour at which each phase begins. */
enum { OW_CLOCK_MORN_HOUR = 5, OW_CLOCK_DAY_HOUR = 9,
       OW_CLOCK_EVE_HOUR = 17, OW_CLOCK_NITE_HOUR = 21 };

typedef struct {
    uint32_t ms;            /* milliseconds into the current game second */
    uint8_t hour, minute, second;
} OwClock;

/* Start the clock at a wall-clock time; the port starts a game at 10:00, inside
 * DAY, because it has no time-set screen to ask. */
void ow_clock_init(OwClock *c, int hour, int minute);

/* Advance by `ms` of real play. Expects a frame delta (a few tens of ms); the
 * multiply is 32-bit, so anything past ~715 s in one call would wrap. */
void ow_clock_advance(OwClock *c, uint32_t ms);

/* The phase an hour belongs to: NITE before 5 and from 21, then MORN, DAY, EVE. */
int ow_clock_tod(int hour);

/* Is an object out in this phase? `mask` is the object_event time-of-day byte:
 * a bit per phase, 0xff for always (home/map_objects.asm CheckObjectTime). */
int ow_clock_object_out(unsigned mask, int tod);

/* The palette group a map uses at `hour`: one of the four phases, or
 * OW_TOD_INDOOR. `map_pal` is the header's time field and `indoor` says whether
 * the map's environment is INDOOR/GATE (PcMapDef.indoor). */
int ow_clock_group(int map_pal, int indoor, int hour);

#endif

#include "ow/clock.h"

void ow_clock_init(OwClock *c, int hour, int minute) {
    c->ms = 0;
    c->second = 0;
    c->minute = (uint8_t)(minute % 60);
    c->hour = (uint8_t)(hour % 24);
}

void ow_clock_advance(OwClock *c, uint32_t ms) {
    c->ms += ms * OW_CLOCK_SPEEDUP;
    while (c->ms >= 1000) {
        c->ms -= 1000;
        if (++c->second < 60) continue;
        c->second = 0;
        if (++c->minute < 60) continue;
        c->minute = 0;
        if (++c->hour >= 24) c->hour = 0;
    }
}

int ow_clock_tod(int hour) {
    if (hour < OW_CLOCK_MORN_HOUR || hour >= OW_CLOCK_NITE_HOUR) return OW_TOD_NITE;
    if (hour < OW_CLOCK_DAY_HOUR) return OW_TOD_MORN;
    if (hour < OW_CLOCK_EVE_HOUR) return OW_TOD_DAY;
    return OW_TOD_EVE;
}

int ow_clock_object_out(unsigned mask, int tod) {
    if (mask == 0xff) return 1;
    return (mask >> (tod & 3)) & 1;
}

/* The phase a map is in: its header's, or the clock's when it says AUTO. */
static int phase_of(int map_pal, int hour) {
    switch (map_pal) {
    case OW_PAL_DAY:  return OW_TOD_DAY;
    case OW_PAL_NITE: return OW_TOD_NITE;
    case OW_PAL_MORN: return OW_TOD_MORN;
    case OW_PAL_EVE:  return OW_TOD_EVE;
    default:          return ow_clock_tod(hour);
    }
}

int ow_clock_group(int map_pal, int indoor, int hour) {
    if (map_pal == OW_PAL_INDOOR) return OW_TOD_INDOOR;
    int phase = phase_of(map_pal, hour);
    /* An interior has its own palette for morning and day, and falls back to the
     * outdoor night colours when the phase is nite or eve
     * (data/maps/environment_colors.asm, .IndoorColors). */
    if (indoor && (phase == OW_TOD_MORN || phase == OW_TOD_DAY)) return OW_TOD_INDOOR;
    return phase;
}

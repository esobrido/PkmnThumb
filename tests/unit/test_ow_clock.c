/* ow_clock: the in-game clock and the palette group an hour picks. */
#include "ow/clock.h"
#include "test_util.h"

/* The four phases, at their boundaries and one hour either side. MORN_HOUR is 5,
 * DAY_HOUR 9, EVE_HOUR 17, NITE_HOUR 21; everything before 5 is NITE, which is
 * GetTimeOfDay's substitution (it falls back to NITE_HOUR for early hours). */
static void test_tod(void) {
    CHECK_EQ(ow_clock_tod(0), OW_TOD_NITE);
    CHECK_EQ(ow_clock_tod(4), OW_TOD_NITE);
    CHECK_EQ(ow_clock_tod(5), OW_TOD_MORN);
    CHECK_EQ(ow_clock_tod(8), OW_TOD_MORN);
    CHECK_EQ(ow_clock_tod(9), OW_TOD_DAY);
    CHECK_EQ(ow_clock_tod(16), OW_TOD_DAY);
    CHECK_EQ(ow_clock_tod(17), OW_TOD_EVE);
    CHECK_EQ(ow_clock_tod(20), OW_TOD_EVE);
    CHECK_EQ(ow_clock_tod(21), OW_TOD_NITE);
    CHECK_EQ(ow_clock_tod(23), OW_TOD_NITE);
}

/* An outdoor map pins its group, or follows the clock (PALETTE_AUTO). */
static void test_group_outdoor(void) {
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 0, 3), OW_TOD_NITE);
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 0, 6), OW_TOD_MORN);
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 0, 12), OW_TOD_DAY);
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 0, 19), OW_TOD_EVE);
    CHECK_EQ(ow_clock_group(OW_PAL_NITE, 0, 12), OW_TOD_NITE);
    CHECK_EQ(ow_clock_group(OW_PAL_MORN, 0, 12), OW_TOD_MORN);
    CHECK_EQ(ow_clock_group(OW_PAL_EVE, 0, 12), OW_TOD_EVE);
    CHECK_EQ(ow_clock_group(OW_PAL_DAY, 0, 23), OW_TOD_DAY);
    /* A value the table does not know follows the clock instead of reading past
     * the end of it. */
    CHECK_EQ(ow_clock_group(9, 0, 12), OW_TOD_DAY);
}

/* Interiors: the interior set by morning and day, the outdoor night colours in
 * the evening and at night (environment_colors.asm .IndoorColors). The map's own
 * PALETTE_DAY keeps a house bright at 23:00, as in the original. */
static void test_group_indoor(void) {
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 1, 6), OW_TOD_INDOOR);
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 1, 12), OW_TOD_INDOOR);
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 1, 19), OW_TOD_EVE);
    CHECK_EQ(ow_clock_group(OW_PAL_AUTO, 1, 23), OW_TOD_NITE);
    CHECK_EQ(ow_clock_group(OW_PAL_DAY, 1, 23), OW_TOD_INDOOR);
    CHECK_EQ(ow_clock_group(OW_PAL_NITE, 1, 23), OW_TOD_NITE);
    CHECK_EQ(ow_clock_group(OW_PAL_INDOOR, 0, 3), OW_TOD_INDOOR);
}

/* An object_event's time byte: 0xff is always there, otherwise one bit per
 * phase. New Bark Town's schoolboy is `(1 << MORN) | (1 << DAY)`, i.e. 3. */
static void test_object_out(void) {
    CHECK(ow_clock_object_out(0xff, OW_TOD_MORN));
    CHECK(ow_clock_object_out(0xff, OW_TOD_NITE));
    CHECK(ow_clock_object_out(3, OW_TOD_MORN));
    CHECK(ow_clock_object_out(3, OW_TOD_DAY));
    CHECK(!ow_clock_object_out(3, OW_TOD_NITE));
    CHECK(!ow_clock_object_out(3, OW_TOD_EVE));
    /* 8 = EVE only, 4 = NITE only */
    CHECK(ow_clock_object_out(8, OW_TOD_EVE));
    CHECK(!ow_clock_object_out(8, OW_TOD_NITE));
    CHECK(ow_clock_object_out(4, OW_TOD_NITE));
    CHECK(!ow_clock_object_out(0, OW_TOD_DAY));
}

/* One real second is six game seconds (NO_RTC_SPEEDUP), so a day is four hours. */
static void test_advance(void) {
    OwClock c;
    ow_clock_init(&c, 10, 0);
    CHECK_EQ(c.hour, 10);
    CHECK_EQ(c.minute, 0);
    CHECK_EQ(c.second, 0);

    /* One real second is six game seconds, so a game minute takes ten real
     * seconds and a game hour ten real minutes (a day in four hours of play). */
    ow_clock_advance(&c, 1000);
    CHECK_EQ(c.second, OW_CLOCK_SPEEDUP);
    ow_clock_advance(&c, 9000);                        /* 54 more game seconds */
    CHECK_EQ(c.second, 0);
    CHECK_EQ(c.minute, 1);

    ow_clock_advance(&c, 10 * 60 * 1000);
    CHECK_EQ(c.hour, 11);
    CHECK_EQ(c.minute, 1);

    /* Frame-sized deltas add up to the same clock as one big one (a 16 ms frame
     * at 6x is 96 ms of game time, which is not a whole second). */
    OwClock d, e;
    ow_clock_init(&d, 10, 0);
    ow_clock_init(&e, 10, 0);
    for (int i = 0; i < 600; i++) ow_clock_advance(&d, 16);
    ow_clock_advance(&e, 9600);
    CHECK_EQ(d.second, e.second);
    CHECK_EQ(d.minute, e.minute);
    CHECK_EQ(d.hour, e.hour);
}

/* Midnight is 0, not 24, and the day wraps there. */
static void test_wrap(void) {
    OwClock c;
    ow_clock_init(&c, 23, 59);
    for (int i = 0; i < 10; i++) ow_clock_advance(&c, 1000);   /* 60 game seconds */
    CHECK_EQ(c.hour, 0);
    CHECK_EQ(c.minute, 0);
    CHECK_EQ(c.second, 0);
    CHECK_EQ(ow_clock_tod(c.hour), OW_TOD_NITE);

    /* init takes any hour; only the clock's own range is 0..23. */
    ow_clock_init(&c, 25, 70);
    CHECK_EQ(c.hour, 1);
    CHECK_EQ(c.minute, 10);
}

int main(void) {
    test_tod();
    test_group_outdoor();
    test_group_indoor();
    test_object_out();
    test_advance();
    test_wrap();
    return test_report("test_ow_clock");
}

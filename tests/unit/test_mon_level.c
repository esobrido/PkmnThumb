/* mon_level: the four experience curves and the level they buy. */
#include "gen/pokemon.h"
#include "mon/level.h"
#include "test_util.h"

/* Gen 2's totals at level 100. These are the numbers every Pokémon game since
 * has kept, so a bad sign or a wrong divisor shows up here immediately. */
static void test_totals(void) {
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_FAST, 100), 1000000u);
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_SLOW, 100), 1059860u);
    CHECK_EQ(level_exp_at(PC_GROWTH_FAST, 100), 800000u);
    CHECK_EQ(level_exp_at(PC_GROWTH_SLOW, 100), 1250000u);

    /* Level 1 costs nothing on every curve. */
    for (int g = 0; g < PC_GROWTH_COUNT; g++) {
        CHECK_EQ(level_exp_at(g, 1), 0u);
        CHECK_EQ(level_exp_at(g, 0), 0u);
    }
}

/* The formula divides the cubic term before adding the signed quadratic:
 * Medium Slow at level 2 is (6 * 8) / 5 - 15 * 4 + 200 - 140 = 9, not 9.6. */
static void test_formula_order(void) {
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_SLOW, 2), 9u);
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_SLOW, 3), 57u);
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_SLOW, 5), 135u);
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_SLOW, 50), 117360u);

    /* The three curves without a quadratic term are n**3 scaled. */
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_FAST, 2), 8u);
    CHECK_EQ(level_exp_at(PC_GROWTH_MEDIUM_FAST, 50), 125000u);
    CHECK_EQ(level_exp_at(PC_GROWTH_FAST, 50), 100000u);
    CHECK_EQ(level_exp_at(PC_GROWTH_SLOW, 50), 156250u);

    /* The curves only ever grow, and never step backwards at the integer
     * divisions (a curve that dips would make level_from_exp skip levels). */
    for (int g = 0; g < PC_GROWTH_COUNT; g++)
        for (int n = 2; n <= MON_MAX_LEVEL; n++)
            CHECK(level_exp_at(g, n) > level_exp_at(g, n - 1));
}

/* CalcLevel's scan: exp buys the highest level whose requirement it reaches. */
static void test_level_from_exp(void) {
    for (int g = 0; g < PC_GROWTH_COUNT; g++) {
        CHECK_EQ(level_from_exp(g, 0), 1);
        CHECK_EQ(level_from_exp(g, level_exp_at(g, 5)), 5);
        CHECK_EQ(level_from_exp(g, level_exp_at(g, 5) - 1), 4);
        CHECK_EQ(level_from_exp(g, level_exp_at(g, 50)), 50);
        CHECK_EQ(level_from_exp(g, level_exp_at(g, 50) + 1), 50);
        /* One short of level 100 is still 99, and the cap holds past the total. */
        CHECK_EQ(level_from_exp(g, level_exp_at(g, 100) - 1), 99);
        CHECK_EQ(level_from_exp(g, level_exp_at(g, 100)), 100);
        CHECK_EQ(level_from_exp(g, 0xffffffffu), 100);
    }
}

static void test_to_next(void) {
    CHECK_EQ(level_exp_to_next(PC_GROWTH_MEDIUM_SLOW, 5, 135), 44u);
    CHECK_EQ(level_exp_to_next(PC_GROWTH_MEDIUM_SLOW, 5, 0), 179u);
    CHECK_EQ(level_exp_to_next(PC_GROWTH_MEDIUM_SLOW, 5, 179), 0u);
    CHECK_EQ(level_exp_to_next(PC_GROWTH_MEDIUM_SLOW, 5, 1000), 0u);
    /* No next level, no requirement: a level 100 mon cannot gain experience. */
    CHECK_EQ(level_exp_to_next(PC_GROWTH_MEDIUM_SLOW, MON_MAX_LEVEL, 0), 0u);
}

/* An unknown growth id is a data error, not a level 1 mon. */
static void test_bad_growth(void) {
    CHECK_EQ(level_exp_at(-1, 50), 0u);
    CHECK_EQ(level_exp_at(PC_GROWTH_COUNT, 50), 0u);
    CHECK_EQ(level_from_exp(-1, 1000000), 0);
    CHECK_EQ(level_exp_to_next(PC_GROWTH_COUNT, 5, 0), 0u);
}

int main(void) {
    test_totals();
    test_formula_order();
    test_level_from_exp();
    test_to_next();
    test_bad_growth();
    return test_report("test_mon_level");
}

/* battle_damage: the Gen 2 formula, one pass at a time. */
#include "battle/damage.h"
#include "battle/type.h"
#include "test_util.h"

static void test_base(void) {
    /* ((2 * 50 / 5 + 2) * 80 * 100) / 100 / 50 + 2 = 37. */
    CHECK_EQ(damage_base(50, 80, 100, 100), 37);
    /* ((2 * 100 / 5 + 2) * 100 * 100) / 100 / 50 + 2 = 86. */
    CHECK_EQ(damage_base(100, 100, 100, 100), 86);
    /* ((2 * 5 / 5 + 2) * 40 * 20) / 20 / 50 + 2 = 5: a level 5 hit is small. */
    CHECK_EQ(damage_base(5, 40, 20, 20), 5);
    /* 2 * 7 / 5 truncates to 2 before the +2, so the factor is 4, not 4.8. */
    CHECK_EQ(damage_base(7, 35, 13, 7), 7);

    /* A defence of 0 would divide by zero; DamagePass1 clamps it to 1. */
    CHECK_EQ(damage_base(5, 40, 20, 0), 66);
    /* A move with no power does no damage, whatever the stats. */
    CHECK_EQ(damage_base(50, 0, 200, 1), 0);
    CHECK_EQ(damage_base(50, -3, 200, 1), 0);
}

static void test_modifiers(void) {
    /* Q4: 32 doubles, 8 halves. */
    CHECK_EQ(damage_modify(37, 32, 100), 74);
    CHECK_EQ(damage_modify(37, 8, 100), 18);   /* 18.5 truncates */
    CHECK_EQ(damage_modify(37, 64, 100), 148); /* x4 */
    CHECK_EQ(damage_modify(37, TYPE_NEUTRAL, 100), 37);

    /* Immune is 0, not the minimum 1: an immunity is not a weak hit. */
    CHECK_EQ(damage_modify(37, 0, 100), 0);
    CHECK_EQ(damage_modify(0, 32, 100), 0);

    /* The roll is 85..100 % and leaves 0 and 1 alone. */
    CHECK_EQ(damage_modify(37, 32, 85), 62); /* 74 * 85 / 100 */
    CHECK_EQ(damage_modify(2, 8, 85), 1);    /* 2 * 8 / 16 = 1, unrolled */
    CHECK_EQ(damage_modify(37, 32, 200), 74); /* clamped to 100 */
    CHECK_EQ(damage_modify(37, 32, 0), 62);   /* clamped to 85 */

    /* A non-immune hit that lands under 1 still takes 1 HP. */
    CHECK_EQ(damage_modify(2, 4, 100), 1);
    CHECK_EQ(damage_modify(1, 8, 100), 1);
}

/* The roll only ever shrinks a hit, and never past 1. */
static void test_roll_range(void) {
    for (int roll = 85; roll <= 100; roll++) {
        int d = damage_modify(50, 32, roll);
        CHECK(d >= 85 && d <= 100);
        CHECK(d <= damage_modify(50, 32, 100));
    }
    for (int q4 = 4; q4 <= 64; q4 *= 2)
        CHECK(damage_modify(3, q4, 85) >= 1);
}

int main(void) {
    test_base();
    test_modifiers();
    test_roll_range();
    return test_report("test_battle_damage");
}

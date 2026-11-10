/* battle_type: the non-neutral pairs of data/types/type_matchups.asm. */
#include "battle/type.h"
#include "gen/pokemon.h"
#include "test_util.h"

static void test_pairs(void) {
    /* A pair that is in the table. */
    CHECK_EQ(type_pair(PC_TYPE_FIRE, PC_TYPE_GRASS), 32);
    CHECK_EQ(type_pair(PC_TYPE_FIRE, PC_TYPE_STEEL), 32);
    CHECK_EQ(type_pair(PC_TYPE_NORMAL, PC_TYPE_ROCK), 8);
    CHECK_EQ(type_pair(PC_TYPE_FAIRY, PC_TYPE_DRAGON), 32);

    /* A pair that is not in the list is neutral. */
    CHECK_EQ(type_pair(PC_TYPE_NORMAL, PC_TYPE_NORMAL), TYPE_NEUTRAL);
    CHECK_EQ(type_pair(PC_TYPE_FIRE, PC_TYPE_NORMAL), TYPE_NEUTRAL);
    CHECK_EQ(type_pair(PC_TYPE_FIRE, PC_TYPE_WATER) == 8, 1);

    /* Immunities are rows too. Ground vs Flying is not: see type_effect. */
    CHECK_EQ(type_pair(PC_TYPE_ELECTRIC, PC_TYPE_GROUND), 0);
    CHECK_EQ(type_pair(PC_TYPE_GROUND, PC_TYPE_FLYING), TYPE_NEUTRAL);
}

static void test_dual_types(void) {
    Mon zard, bulb, gengar;
    mon_init(&zard, PC_SPECIES_MOLTRES, 50, 0);   /* fire / flying */
    mon_init(&bulb, PC_SPECIES_BULBASAUR, 5, 0);   /* grass / poison */
    mon_init(&gengar, PC_SPECIES_GENGAR, 50, 0);   /* ghost / poison */

    /* One doubled type, one neutral one. */
    CHECK_EQ(type_effect(&zard, PC_TYPE_WATER), 32);
    CHECK_EQ(type_effect(&zard, PC_TYPE_ELECTRIC), 32);

    /* Both types double: truly x4, which a float product could round away. */
    CHECK_EQ(type_effect(&zard, PC_TYPE_ROCK), 64);

    /* An immunity ends the walk, even though Ground doubles Fire: this one is
     * the airborne check, not a table row. */
    CHECK_EQ(type_effect(&zard, PC_TYPE_GROUND), 0);

    /* Halving accumulates: grass and poison each halve x1 to x0.25. */
    CHECK_EQ(type_effect(&bulb, PC_TYPE_GRASS), 4);
    CHECK_EQ(type_effect(&bulb, PC_TYPE_FIRE), 32);
    CHECK_EQ(type_effect(&bulb, PC_TYPE_ELECTRIC), 8);
    CHECK_EQ(type_effect(&bulb, PC_TYPE_WATER), 8);

    /* Normal does nothing to a Ghost, whatever the other type says. */
    CHECK_EQ(type_effect(&gengar, PC_TYPE_NORMAL), 0);
    CHECK_EQ(type_effect(&gengar, PC_TYPE_PSYCHIC), 32);

    /* No defender is neutral, not a crash. */
    CHECK_EQ(type_effect(NULL, PC_TYPE_FIRE), TYPE_NEUTRAL);
}

/* The generated table only holds the three Q4 factors, and no pair twice. */
static void test_table(void) {
    CHECK(PC_TYPE_MATCHUP_COUNT > 100);
    for (int i = 0; i < PC_TYPE_MATCHUP_COUNT; i++) {
        int mod = pc_type_matchups[i].mod;
        CHECK(mod == 0 || mod == 8 || mod == 32);
        CHECK(pc_type_matchups[i].attack < PC_TYPE_UNKNOWN_T);
        CHECK(pc_type_matchups[i].defend < PC_TYPE_UNKNOWN_T);
        for (int j = i + 1; j < PC_TYPE_MATCHUP_COUNT; j++)
            CHECK(!(pc_type_matchups[i].attack == pc_type_matchups[j].attack &&
                    pc_type_matchups[i].defend == pc_type_matchups[j].defend));
    }
    /* The last rows are the ones Foresight skips: the two Ghost immunities. */
    CHECK_EQ(PC_TYPE_MATCHUP_FORESIGHT_COUNT, 2);
    for (int i = PC_TYPE_MATCHUP_COUNT - PC_TYPE_MATCHUP_FORESIGHT_COUNT;
         i < PC_TYPE_MATCHUP_COUNT; i++) {
        CHECK_EQ(pc_type_matchups[i].mod, 0);
        CHECK_EQ(pc_type_matchups[i].defend, PC_TYPE_GHOST);
    }
}

/* Every attacking type against every defending type stays in the Q4 grid. */
static void test_all_types(void) {
    for (int a = 0; a < PC_TYPE_UNKNOWN_T; a++) {
        CHECK_EQ(type_pair(a, a) == 0 || type_pair(a, a) == 8 ||
                 type_pair(a, a) == 32 || type_pair(a, a) == TYPE_NEUTRAL, 1);
    }
}

int main(void) {
    test_pairs();
    test_dual_types();
    test_table();
    test_all_types();
    return test_report("test_battle_type");
}

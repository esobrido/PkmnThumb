/* mon_mon: a Pokémon's stats, types, name and moves. */
#include "mon/mon.h"
#include "test_util.h"

/* The stat formula is CalcPkmnStatC from engine/pokemon/mon_stats.asm:
 *   stat = ((2 * (base + DV) + 1) * level) / 100 + 5
 *   HP   = the same, plus level + 10
 * with the HP DV derived from the low bits of the other four. */
static void test_stats(void) {
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    CHECK_EQ(b.species, PC_SPECIES_BULBASAUR);
    CHECK_EQ(b.level, 5);
    CHECK_EQ(b.stat[MON_HP], 19);      /* (2*45+1)*5/100 = 4, + 5 + 10 */
    CHECK_EQ(b.stat[MON_ATK], 9);      /* (2*49+1)*5/100 = 4, + 5 */
    CHECK_EQ(b.stat[MON_DEF], 9);
    CHECK_EQ(b.stat[MON_SPE], 9);      /* (2*45+1)*5/100 = 4 */
    CHECK_EQ(b.stat[MON_SAT], 11);     /* (2*65+1)*5/100 = 6 */
    CHECK_EQ(b.stat[MON_SDF], 11);
    /* A new mon arrives at full HP. */
    CHECK_EQ(b.hp, b.stat[MON_HP]);
    /* A mon made at level 5 already has the Medium Slow total for level 5:
     * (6 * 125) / 5 - 15 * 25 + 500 - 140 = 135. */
    CHECK_EQ(b.exp, 135);
    CHECK_EQ(b.status, 0);

    /* All DVs at 15: the HP DV is 15 too, and every stat grows. */
    Mon perfect;
    mon_init(&perfect, PC_SPECIES_BULBASAUR, 5, 0xffff);
    CHECK_EQ(perfect.stat[MON_HP], 21);   /* (2*60+1)*5/100 = 6, + 15 */
    CHECK_EQ(perfect.stat[MON_ATK], 11);  /* (2*64+1)*5/100 = 6, + 5 */

    /* Level 100 is where the formula is easiest to check by hand. */
    Mon l100;
    mon_init(&l100, PC_SPECIES_BULBASAUR, 100, 0);
    CHECK_EQ(l100.stat[MON_HP], 201);     /* 91 + 100 + 10 */
    CHECK_EQ(l100.stat[MON_ATK], 104);    /* 99 + 5 */
    CHECK_EQ(l100.stat[MON_SAT], 136);    /* (2*65+1)*100/100 = 131, + 5 */
}

/* The HP DV is not free: it is bit 0 of attack/defence/speed/special. A DV word
 * of 0x1111 (all four odd) therefore has an HP DV of 15, and 0x0000 has 0. */
static void test_hp_dv(void) {
    Mon a, b;
    mon_init(&a, PC_SPECIES_PIKACHU, 50, 0x1111);
    mon_init(&b, PC_SPECIES_PIKACHU, 50, 0x2222);
    CHECK_EQ(a.stat[MON_HP], b.stat[MON_HP] + 15);  /* HP DV 15 vs 0 */
    /* ... while attack follows its own nibble, not the HP DV. */
    CHECK(a.stat[MON_ATK] < b.stat[MON_ATK]);
}

/* Types come from the species, including the repeated slot of a one-type mon. */
static void test_types(void) {
    Mon b, p;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    CHECK_EQ(mon_type(&b, 0), PC_TYPE_GRASS);
    CHECK_EQ(mon_type(&b, 1), PC_TYPE_POISON);
    CHECK(mon_has_type(&b, PC_TYPE_GRASS));
    CHECK(mon_has_type(&b, PC_TYPE_POISON));
    CHECK(!mon_has_type(&b, PC_TYPE_FIRE));

    mon_init(&p, PC_SPECIES_PIKACHU, 5, 0);
    CHECK_EQ(mon_type(&p, 0), PC_TYPE_ELECTRIC);
    CHECK_EQ(mon_type(&p, 1), PC_TYPE_ELECTRIC);
    CHECK(!mon_has_type(&p, PC_TYPE_NORMAL));
}

/* The base stats are the species', in the same order the game stores them. */
static void test_base(void) {
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    CHECK_EQ(mon_base_stat(&b, MON_HP), 45);
    CHECK_EQ(mon_base_stat(&b, MON_ATK), 49);
    CHECK_EQ(mon_base_stat(&b, MON_SAT), 65);
    CHECK_EQ(mon_base_stat(&b, MON_STAT_COUNT), 0);
}

/* A name is the species' charmap bytes until a nickname replaces it. */
static void test_name(void) {
    Mon b;
    int len = -1;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    const uint8_t *n = mon_name(&b, &len);
    CHECK(n != 0);
    CHECK_EQ(len, 9);                       /* "Bulbasaur" */
    /* 'B' is $81 and 'a' is $b4 in the game's charmap: not ASCII. */
    CHECK_EQ(n[0], 0x81);
    CHECK_EQ(n[1], 0xb4);

    b.nickname[0] = 0x83;                   /* 'D' */
    b.nickname[1] = 0xb4;
    n = mon_name(&b, &len);
    CHECK_EQ(len, 2);
    CHECK(n == b.nickname);
}

/* Teaching a move fills its PP from the move table, and an empty slot has none. */
static void test_moves(void) {
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    CHECK_EQ(b.moves[0], 0);
    CHECK_EQ(b.pp[0], 0);
    mon_set_move(&b, 0, PC_MOVE_TACKLE);
    CHECK_EQ(b.moves[0], PC_MOVE_TACKLE);
    CHECK_EQ(b.pp[0], 35);                  /* Tackle's PP */
    mon_set_move(&b, 3, PC_MOVE_STRUGGLE);
    CHECK_EQ(b.pp[3], 1);
    /* Out-of-range slots and ids are ignored, not written out of bounds. */
    mon_set_move(&b, -1, PC_MOVE_TACKLE);
    mon_set_move(&b, MON_MOVES, PC_MOVE_TACKLE);
    mon_set_move(&b, 1, PC_MOVE_COUNT + 1);
    CHECK_EQ(b.moves[1], 0);
    CHECK_EQ(b.pp[1], 0);
}

/* Healing undoes damage and status but not spent PP (that is an Ether). */
static void test_heal(void) {
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 20, 0x1234);
    mon_set_move(&b, 0, PC_MOVE_TACKLE);
    b.hp = 3;
    b.pp[0] = 1;
    b.status = 8;                            /* poisoned, in the original's bits */
    mon_heal(&b);
    CHECK_EQ(b.hp, b.stat[MON_HP]);
    CHECK_EQ(b.status, 0);
    CHECK_EQ(b.pp[0], 1);
}

/* Recalculating keeps the damage taken and clamps it to the new maximum. */
static void test_recalc(void) {
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 20, 0);
    b.hp = b.stat[MON_HP] - 5;
    uint16_t max20 = b.stat[MON_HP];
    b.level = 30;
    mon_recalc(&b);
    CHECK(b.stat[MON_HP] > max20);
    CHECK_EQ(b.hp, max20 - 5);

    /* A level that shrinks the maximum cannot leave hp above it. */
    b.level = 2;
    mon_recalc(&b);
    CHECK_EQ(b.hp, b.stat[MON_HP]);
}

/* Experience buys levels one at a time, and the HP moves with the maximum by the
 * difference, as UpdatePkmnStats does: (MaxHP on level up) + (damage taken). */
static void test_exp(void) {
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    CHECK_EQ(mon_growth(&b), PC_GROWTH_MEDIUM_SLOW);
    CHECK_EQ(mon_exp_for_level(&b, 5), 135u);
    CHECK_EQ(mon_exp_for_level(&b, 6), 179u);
    CHECK_EQ(mon_exp_to_next(&b), 44u);            /* 179 - 135 */

    b.hp = 10;
    CHECK_EQ(mon_add_exp(&b, 43), 0);              /* one short */
    CHECK_EQ(b.level, 5);
    CHECK_EQ(b.exp, 178);
    CHECK_EQ(b.hp, 10);

    CHECK_EQ(mon_add_exp(&b, 1), 1);
    CHECK_EQ(b.level, 6);
    CHECK_EQ(b.exp, 179);
    CHECK_EQ(b.stat[MON_HP], 21);                  /* (2*45+1)*6/100 = 5, + 16 */
    CHECK_EQ(b.hp, 12);                            /* 10 + (21 - 19) */
    CHECK_EQ(mon_exp_to_next(&b), 57u);            /* level 7 costs 236 */

    /* One award can jump several levels; the return value is the count, so the
     * caller can announce each one. */
    Mon jump;
    mon_init(&jump, PC_SPECIES_BULBASAUR, 5, 0);
    int levels = mon_add_exp(&jump, 5000);
    CHECK(levels > 1);
    CHECK_EQ(jump.level, 5 + levels);
    CHECK(mon_exp_for_level(&jump, jump.level) <= jump.exp);
    CHECK(jump.exp < mon_exp_for_level(&jump, jump.level + 1));
    /* The maximum grew by more than the HP did only if the mon was hurt; a full
     * mon stays full through any number of levels. */
    CHECK_EQ(jump.hp, jump.stat[MON_HP]);

    /* A fainted mon levels up but does not come back. */
    Mon dead;
    mon_init(&dead, PC_SPECIES_BULBASAUR, 5, 0);
    dead.hp = 0;
    CHECK_EQ(mon_add_exp(&dead, 44), 1);
    CHECK_EQ(dead.level, 6);
    CHECK_EQ(dead.hp, 0);

    /* Level 100 is the end: experience is clamped to the total and no more
     * levels are handed out. */
    Mon top;
    mon_init(&top, PC_SPECIES_BULBASAUR, 99, 0);
    CHECK_EQ(mon_add_exp(&top, 1000000000u), 1);
    CHECK_EQ(top.level, MON_MAX_LEVEL);
    CHECK_EQ(top.exp, 1059860u);
    CHECK_EQ(mon_exp_to_next(&top), 0u);
    CHECK_EQ(mon_add_exp(&top, 1000), 0);
    CHECK_EQ(top.exp, 1059860u);

    /* An empty mon cannot gain experience either. */
    Mon none;
    mon_init(&none, 0, 5, 0);
    CHECK_EQ(mon_growth(&none), -1);
    CHECK_EQ(mon_add_exp(&none, 1000), 0);
}

/* Level-up moves: every move in the species' list it already knows at this
 * level, keeping the last four (GiveMonInitialMoveset). */
static void test_level_moves(void) {
    Mon c;
    mon_init(&c, PC_SPECIES_CYNDAQUIL, 5, 0);
    mon_init_moves(&c);
    CHECK_EQ(c.moves[0], PC_MOVE_TACKLE);
    CHECK_EQ(c.moves[1], PC_MOVE_LEER);
    CHECK_EQ(c.moves[2], 0);
    CHECK_EQ(c.pp[0], pc_moves[PC_MOVE_TACKLE].pp);

    mon_init(&c, PC_SPECIES_CYNDAQUIL, 10, 0);
    mon_init_moves(&c);
    CHECK_EQ(c.moves[0], PC_MOVE_TACKLE);
    CHECK_EQ(c.moves[1], PC_MOVE_LEER);
    CHECK_EQ(c.moves[2], PC_MOVE_SMOKESCREEN);
    CHECK_EQ(c.moves[3], PC_MOVE_EMBER);

    /* A fifth move pushes the oldest out, so the four newest remain. */
    mon_init(&c, PC_SPECIES_CYNDAQUIL, 13, 0);
    mon_init_moves(&c);
    CHECK_EQ(c.moves[0], PC_MOVE_LEER);
    CHECK_EQ(c.moves[1], PC_MOVE_SMOKESCREEN);
    CHECK_EQ(c.moves[3], PC_MOVE_QUICK_ATTACK);

    /* A wild Rattata's list, straight from data/pokemon/evos_attacks.asm. */
    Mon r;
    mon_init(&r, PC_SPECIES_RATTATA, 3, 0);
    mon_init_moves(&r);
    CHECK_EQ(r.moves[0], PC_MOVE_TACKLE);
    CHECK_EQ(r.moves[1], PC_MOVE_LEER);

    /* A species with no extracted learnset gets nothing; the battle falls
     * back to Struggle. */
    Mon b;
    mon_init(&b, PC_SPECIES_BULBASAUR, 5, 0);
    mon_init_moves(&b);
    CHECK_EQ(b.moves[0], 0);
}

/* An empty slot is a mon with no species: no stats, no name, no type. */
static void test_empty(void) {
    Mon m;
    mon_init(&m, 0, 5, 0);
    int len = 99;
    CHECK_EQ(m.stat[MON_HP], 0);
    CHECK_EQ(m.hp, 0);
    CHECK(mon_name(&m, &len) == 0);
    CHECK_EQ(len, 0);
    CHECK_EQ(mon_type(&m, 0), -1);
    CHECK_EQ(mon_base_stat(&m, MON_HP), 0);
}

int main(void) {
    test_stats();
    test_hp_dv();
    test_types();
    test_base();
    test_name();
    test_moves();
    test_heal();
    test_recalc();
    test_exp();
    test_level_moves();
    test_empty();
    return test_report("test_mon_mon");
}

/* battle_turn: turn order, one hit each, and the experience of a KO. */
#include "battle/turn.h"
#include "gen/moves.h"
#include "gen/pokemon.h"
#include "test_util.h"

static void make(Mon *m, int species, int level, int move0, int move1) {
    mon_init(m, species, level, 0);
    if (move0)
        mon_set_move(m, 0, move0);
    if (move1)
        mon_set_move(m, 1, move1);
}

static void test_exp_reward(void) {
    Mon wild, empty;
    make(&wild, PC_SPECIES_RATTATA, 7, PC_MOVE_TACKLE, 0);
    /* base_exp * level / 7: Rattata pays 57 at any level multiple of 7. */
    CHECK_EQ(battle_exp_reward(&wild), 57u);
    wild.level = 1;
    CHECK_EQ(battle_exp_reward(&wild), 8u);
    wild.level = 100;
    CHECK_EQ(battle_exp_reward(&wild), 814u);

    CHECK_EQ(battle_exp_reward(NULL), 0u);
    empty.species = 0;
    CHECK_EQ(battle_exp_reward(&empty), 0u);
}

static void test_priorities(void) {
    CHECK_EQ(battle_priority(PC_MOVE_QUICK_ATTACK), 1);
    CHECK_EQ(battle_priority(PC_MOVE_COUNTER), -5);
    CHECK_EQ(battle_priority(PC_MOVE_TACKLE), 0);
    /* NO_MOVE is in the table with 10: it is the switch-out pseudo-move. */
    CHECK_EQ(battle_priority(0), 10);
    CHECK_EQ(battle_priority(PC_MOVE_COUNT), 0);
}

static void test_turn(void) {
    Mon p, w;
    Battle b;
    BattleTurn t;
    make(&p, PC_SPECIES_BULBASAUR, 5, PC_MOVE_TACKLE, 0);
    make(&w, PC_SPECIES_RATTATA, 3, PC_MOVE_TACKLE, 0);
    p.stat[MON_SPE] = 100;
    w.stat[MON_SPE] = 10;

    battle_start(&b, &p, &w, 1);
    uint16_t php = p.hp, whp = w.hp;
    battle_turn(&b, 0, &t);

    CHECK_EQ(t.count, 2);
    CHECK_EQ(t.outcome, BATTLE_ONGOING);
    CHECK_EQ(t.hit[0].side, 0); /* the player is faster */
    CHECK_EQ(t.hit[1].side, 1);
    /* Both spent a PP and both hits took HP off the other mon. */
    CHECK_EQ(p.pp[0], 34);
    CHECK_EQ(w.pp[0], 34);
    CHECK_EQ(w.hp, whp - t.hit[0].damage);
    CHECK_EQ(p.hp, php - t.hit[1].damage);
    CHECK_EQ(t.hit[0].move, PC_MOVE_TACKLE);
    /* Tackle is Normal and Bulbasaur is Grass/Poison: no STAB, neutral. */
    CHECK_EQ(t.hit[0].effect, 16);
    CHECK(t.hit[0].damage > 0);
    CHECK_EQ(t.exp, 0u);

    /* The slower mon goes first when the speeds say so. */
    p.stat[MON_SPE] = 1;
    w.stat[MON_SPE] = 200;
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.hit[0].side, 1);
    CHECK_EQ(t.hit[1].side, 0);

    /* Priority beats speed: Quick Attack goes first from the slower mon. */
    p.stat[MON_SPE] = 1;
    w.stat[MON_SPE] = 200;
    mon_set_move(&p, 0, PC_MOVE_QUICK_ATTACK);
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.hit[0].side, 0);
    CHECK_EQ(t.hit[0].move, PC_MOVE_QUICK_ATTACK);
}

static void test_types_in_battle(void) {
    Mon p, w;
    Battle b;
    BattleTurn t;

    /* Normal against a Ghost is an immunity: 0 damage, not 1. */
    make(&p, PC_SPECIES_BULBASAUR, 30, PC_MOVE_TACKLE, 0);
    make(&w, PC_SPECIES_GENGAR, 30, PC_MOVE_GROWL, 0);
    p.stat[MON_SPE] = 100;
    w.stat[MON_SPE] = 1;
    battle_start(&b, &p, &w, 7);
    uint16_t whp = w.hp;
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.hit[0].effect, 0);
    CHECK_EQ(t.hit[0].damage, 0);
    CHECK_EQ(w.hp, whp);

    /* Rock against Fire/Flying is x4 and is reported as such. */
    make(&p, PC_SPECIES_BULBASAUR, 30, PC_MOVE_ROCK_THROW, 0);
    make(&w, PC_SPECIES_MOLTRES, 30, PC_MOVE_GROWL, 0);
    p.stat[MON_SPE] = 100;
    w.stat[MON_SPE] = 1;
    battle_start(&b, &p, &w, 7);
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.hit[0].effect, 64);
    CHECK(t.hit[0].damage > 0);
}

static void test_win(void) {
    Mon p, w;
    Battle b;
    BattleTurn t;
    make(&p, PC_SPECIES_BULBASAUR, 5, PC_MOVE_TACKLE, 0);
    make(&w, PC_SPECIES_RATTATA, 7, PC_MOVE_GROWL, 0);
    p.stat[MON_SPE] = 100;
    w.stat[MON_SPE] = 1;
    w.hp = 1;
    uint32_t exp = p.exp;

    battle_start(&b, &p, &w, 3);
    battle_turn(&b, 0, &t);

    CHECK_EQ(t.count, 1); /* the wild mon fainted, it does not answer */
    CHECK_EQ(t.outcome, BATTLE_WON);
    CHECK_EQ(b.outcome, BATTLE_WON);
    CHECK_EQ(t.exp, 57u); /* base_exp 57 * level 7 / 7 */
    /* Bulbasaur at level 5 has 135 exp and needs 179 for level 6. */
    CHECK_EQ(p.exp, exp + 57u);
    CHECK_EQ(p.level, 6);
    CHECK_EQ(t.levels, 1);
    CHECK_EQ(w.hp, 0);
    CHECK_EQ(p.pp[0], 34);

    /* A finished battle accepts no more turns. */
    BattleTurn again;
    battle_turn(&b, 0, &again);
    CHECK_EQ(again.count, 0);
    CHECK_EQ(again.outcome, BATTLE_WON);
    CHECK_EQ(again.exp, 0u);
}

static void test_loss(void) {
    Mon p, w;
    Battle b;
    BattleTurn t;
    make(&p, PC_SPECIES_BULBASAUR, 5, PC_MOVE_GROWL, 0);
    make(&w, PC_SPECIES_RATTATA, 30, PC_MOVE_TACKLE, 0);
    p.stat[MON_SPE] = 1;
    w.stat[MON_SPE] = 200;
    p.hp = 1;

    battle_start(&b, &p, &w, 11);
    battle_turn(&b, 0, &t);

    CHECK_EQ(t.count, 1);
    CHECK_EQ(t.outcome, BATTLE_LOST);
    CHECK_EQ(p.hp, 0);
    CHECK_EQ(t.exp, 0u);
    CHECK_EQ(t.levels, 0);
}

/* A slot with no PP falls back to one that has it; none at all is Struggle. */
static void test_pp_fallback(void) {
    Mon p, w;
    Battle b;
    BattleTurn t;
    make(&p, PC_SPECIES_BULBASAUR, 10, PC_MOVE_TACKLE, PC_MOVE_GROWL);
    make(&w, PC_SPECIES_RATTATA, 10, PC_MOVE_GROWL, 0);
    p.stat[MON_SPE] = 100;
    w.stat[MON_SPE] = 1;
    p.pp[0] = 0;

    battle_start(&b, &p, &w, 5);
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.hit[0].move, PC_MOVE_GROWL);
    CHECK_EQ(p.pp[1], 39); /* Growl's PP is 40 */
    CHECK_EQ(p.pp[0], 0);

    /* Everything empty: Struggle, and no PP is taken from an empty slot. */
    p.pp[0] = p.pp[1] = 0;
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.hit[0].move, PC_MOVE_STRUGGLE);
    CHECK_EQ(p.pp[0], 0);
    CHECK_EQ(p.pp[1], 0);
}

/* At level 100 the game gives no experience, so neither does the port. */
static void test_level_100(void) {
    Mon p, w;
    Battle b;
    BattleTurn t;
    make(&p, PC_SPECIES_BULBASAUR, 100, PC_MOVE_TACKLE, 0);
    make(&w, PC_SPECIES_RATTATA, 2, PC_MOVE_GROWL, 0);
    p.stat[MON_SPE] = 100;
    w.stat[MON_SPE] = 1;
    w.hp = 1;

    battle_start(&b, &p, &w, 9);
    battle_turn(&b, 0, &t);
    CHECK_EQ(t.outcome, BATTLE_WON);
    CHECK_EQ(t.exp, 0u);
    CHECK_EQ(t.levels, 0);
    CHECK_EQ(p.level, 100);
}

int main(void) {
    test_exp_reward();
    test_priorities();
    test_turn();
    test_types_in_battle();
    test_win();
    test_loss();
    test_pp_fallback();
    test_level_100();
    return test_report("test_battle_turn");
}

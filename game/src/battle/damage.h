/*
 * battle/damage.h — the Gen 2 damage formula, as numbers only.
 *
 * `BattleCommand_damagecalc` in the original is four passes over a 24-bit
 * accumulator, and every division truncates where it happens: level * 2 / 5,
 * times power, times attack, divided by defence, divided by 50, plus 2. Doing
 * it as one expression with floating point gives different numbers on the low
 * end, where most first-route fights live.
 *
 * Type effectiveness and STAB come after the +2 (`BattleCommand_stab`), both as
 * a single Q4 multiplier, then the 85..100 roll of `damagevariation`. A hit
 * that is neither immune nor zero-power always takes at least 1 HP; the
 * original's roll skips values of 1 precisely because reducing it would make it
 * 0.
 *
 * Pure: numbers in, a number out.
 */
#ifndef BATTLE_DAMAGE_H
#define BATTLE_DAMAGE_H

/* (((2 * level / 5 + 2) * power * attack) / defense) / 50 + 2.
 * A defence of 0 is treated as 1, as DamagePass1 does. */
int damage_base(int level, int power, int attack, int defense);

/* Apply the Q4 modifier (16 = x1, 0 = immune) and the 85..100 roll.
 * Returns 0 only for an immune hit or a move with no power. */
int damage_modify(int base, int q4, int roll);

#endif

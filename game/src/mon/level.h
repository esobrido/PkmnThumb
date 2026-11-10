/*
 * mon/level.h — experience curves.
 *
 * The four curves are the coefficients the original keeps in
 * `data/growth_rates.asm`, and `level_exp_at` evaluates them the way
 * `CalcExpAtLevel` (engine/pokemon/experience.asm) does:
 *
 *     exp(n) = (a * n^3) / b + c * n^2 + d * n - e      (n copies of the level)
 *
 * with the cubic term divided *before* the signed quadratic is added: Medium
 * Slow at level 2 is 9, not 9.6 rounded, and at level 100 the four curves land
 * on the totals the Gen 2 games use (1 000 000 / 1 059 860 / 800 000 / 1 250 000).
 *
 * Level 1 is always 0 experience, and 100 is the cap (`MAX_LEVEL` in the
 * original). Nothing here knows about a `Mon`: a growth id, a level and an
 * experience total go in, numbers come out.
 */
#ifndef MON_LEVEL_H
#define MON_LEVEL_H

#include <stdint.h>

/* MAX_LEVEL in the original. */
#define MON_MAX_LEVEL 100

/* Experience needed to be at `level` on that curve (0 below level 2, capped at
 * level 100). An out-of-range growth id or a level below 1 gives 0. */
uint32_t level_exp_at(int growth, int level);

/* The level an experience total buys on that curve: the highest level whose
 * requirement is not above `exp`, 1 at the bottom and 100 at the top. This is
 * CalcLevel's scan, and it is what lets a single award jump several levels. */
int level_from_exp(int growth, uint32_t exp);

/* What is still missing for the next level (0 at level 100, and never negative
 * if `exp` is already past the requirement). */
uint32_t level_exp_to_next(int growth, int level, uint32_t exp);

#endif

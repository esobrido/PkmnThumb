#include "mon/level.h"

#include "gen/pokemon.h"

static const PcGrowth *growth_of(int growth) {
    if (growth < 0 || growth >= PC_GROWTH_COUNT) return 0;
    return &pc_growth_rates[growth];
}

uint32_t level_exp_at(int growth, int level) {
    if (level <= 1) return 0;
    if (level > MON_MAX_LEVEL) level = MON_MAX_LEVEL;
    const PcGrowth *g = growth_of(growth);
    if (!g) return 0;

    /* (a * n**3) / b, integer-divided before anything else is added, and only
     * then the signed quadratic term: CalcExpAtLevel pushes the cubic quotient
     * to the stack for exactly this reason. */
    uint32_t n = (uint32_t)level;
    int32_t v = (int32_t)((uint32_t)g->a * n * n * n / g->b);
    v += (int32_t)g->c * (int32_t)(n * n);
    v += (int32_t)g->d * (int32_t)n;
    v -= (int32_t)g->e;
    return v < 0 ? 0 : (uint32_t)v;
}

int level_from_exp(int growth, uint32_t exp) {
    if (!growth_of(growth)) return 0;
    int level = 1;
    /* CalcLevel raises the candidate until the requirement passes the total and
     * then steps back once, so a jump of several levels resolves in one go. */
    for (int n = 2; n <= MON_MAX_LEVEL; n++) {
        if (exp < level_exp_at(growth, n)) break;
        level = n;
    }
    return level;
}

uint32_t level_exp_to_next(int growth, int level, uint32_t exp) {
    if (level >= MON_MAX_LEVEL) return 0;
    uint32_t need = level_exp_at(growth, level + 1);
    return need > exp ? need - exp : 0;
}

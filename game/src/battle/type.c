/* battle/type.c — the pair table scan of _CheckTypeMatchup. */
#include "battle/type.h"

#include <stddef.h>

#include "gen/pokemon.h"

int type_pair(int attack_type, int defend_type) {
    for (int i = 0; i < PC_TYPE_MATCHUP_COUNT; i++) {
        const PcTypeMatchup *row = &pc_type_matchups[i];
        if (row->attack == attack_type && row->defend == defend_type)
            return row->mod;
    }
    return TYPE_NEUTRAL;
}

int type_effect(const Mon *defender, int attack_type) {
    if (defender == NULL)
        return TYPE_NEUTRAL;
    int q4 = TYPE_NEUTRAL;
    int t1 = mon_type(defender, 0);
    int t2 = mon_type(defender, 1);
    /* Ground vs Flying is not a row: the original comments it out because
     * `CheckAirborne` answers instead (Flying, Levitate, Magnet Rise). The port
     * knows no abilities yet, so it uses the type. */
    if (attack_type == PC_TYPE_GROUND &&
        (t1 == PC_TYPE_FLYING || t2 == PC_TYPE_FLYING))
        return 0;
    for (int i = 0; i < PC_TYPE_MATCHUP_COUNT; i++) {
        const PcTypeMatchup *row = &pc_type_matchups[i];
        if (row->attack != attack_type)
            continue;
        if (row->defend != t1 && row->defend != t2)
            continue;
        /* The original stops at the first 0: no later doubling can undo it. */
        if (row->mod == 0)
            return 0;
        if (row->mod == 2 * TYPE_NEUTRAL)
            q4 *= 2;
        else if (row->mod == TYPE_NEUTRAL / 2)
            q4 /= 2;
    }
    return q4;
}

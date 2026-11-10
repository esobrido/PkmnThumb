/* battle/damage.c — DamagePass1..4 and the two modifiers that follow them. */
#include "battle/damage.h"

#include "battle/type.h"

int damage_base(int level, int power, int attack, int defense) {
    if (power <= 0)
        return 0;
    if (level < 1)
        level = 1;
    if (defense < 1)
        defense = 1;
    if (attack < 0)
        attack = 0;
    int d = (2 * level / 5 + 2) * power * attack;
    d /= defense;
    d /= 50;
    return d + 2;
}

int damage_modify(int base, int q4, int roll) {
    if (base <= 0 || q4 <= 0)
        return 0;
    int d = base * q4 / TYPE_NEUTRAL;
    /* damagevariation leaves 0 and 1 alone: the roll only shrinks, and a 1 that
     * rolled 85 would become 0. */
    if (d > 1) {
        if (roll < 85)
            roll = 85;
        if (roll > 100)
            roll = 100;
        d = d * roll / 100;
    }
    return d < 1 ? 1 : d;
}

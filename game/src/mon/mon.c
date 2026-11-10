#include "mon/mon.h"

static const PcSpecies *species_of(int species) {
    if (species <= 0 || species >= PC_SPECIES_COUNT) return 0;
    const PcSpecies *sp = &pc_species[species];
    return sp->base[MON_HP] ? sp : 0;
}

void mon_recalc(Mon *m) {
    const PcSpecies *sp = species_of(m->species);
    if (!sp) {
        for (int i = 0; i < MON_STAT_COUNT; i++) m->stat[i] = 0;
        m->hp = 0;
        return;
    }

    /* The four DVs as the game packs them, then the HP DV derived from their
     * low bits — the same rule as GetDVByGenes. */
    int atk = (m->dv >> 12) & 15;
    int def = (m->dv >> 8) & 15;
    int spe = (m->dv >> 4) & 15;
    int spc = m->dv & 15;
    int dv[MON_STAT_COUNT];
    dv[MON_HP] = ((atk & 1) << 3) | ((def & 1) << 2) | ((spe & 1) << 1) | (spc & 1);
    dv[MON_ATK] = atk;
    dv[MON_DEF] = def;
    dv[MON_SPE] = spe;
    dv[MON_SAT] = spc;
    dv[MON_SDF] = spc;

    /* CalcPkmnStatC: ((2 * (base + DV) + 1) * level) / 100, plus level + 10 for
     * HP and 5 for everything else. The + 1 is the game's own (it stands in for
     * the stat experience term, which the port does not carry yet). */
    uint16_t old_max = m->stat[MON_HP];
    for (int i = 0; i < MON_STAT_COUNT; i++) {
        int v = (2 * ((int)sp->base[i] + dv[i]) + 1) * m->level / 100;
        m->stat[i] = (uint16_t)(v + (i == MON_HP ? m->level + 10 : 5));
    }

    if (old_max == 0 || m->hp > m->stat[MON_HP]) m->hp = m->stat[MON_HP];
}

void mon_init(Mon *m, int species, int level, uint16_t dv) {
    m->species = (uint16_t)species;
    m->level = (uint8_t)(level < 1 ? 1 : level > 100 ? 100 : level);
    m->dv = dv;
    m->exp = 0;
    m->status = 0;
    for (int i = 0; i < MON_MOVES; i++) {
        m->moves[i] = 0;
        m->pp[i] = 0;
    }
    for (int i = 0; i < MON_NICK_LEN; i++) m->nickname[i] = 0;
    for (int i = 0; i < MON_STAT_COUNT; i++) m->stat[i] = 0;
    m->hp = 0;
    mon_recalc(m);
    m->hp = m->stat[MON_HP];
}

int mon_base_stat(const Mon *m, int which) {
    const PcSpecies *sp = species_of(m->species);
    if (!sp || which < 0 || which >= MON_STAT_COUNT) return 0;
    return sp->base[which];
}

int mon_type(const Mon *m, int i) {
    const PcSpecies *sp = species_of(m->species);
    if (!sp) return -1;
    return sp->type[i & 1];
}

int mon_has_type(const Mon *m, int type) {
    return mon_type(m, 0) == type || mon_type(m, 1) == type;
}

const uint8_t *mon_name(const Mon *m, int *len) {
    if (m->nickname[0]) {
        int n = 0;
        while (n < MON_NICK_LEN - 1 && m->nickname[n]) n++;
        if (len) *len = n;
        return m->nickname;
    }
    const PcSpecies *sp = species_of(m->species);
    if (!sp) {
        if (len) *len = 0;
        return 0;
    }
    if (len) *len = sp->name_len;
    return &pc_species_names[sp->name];
}

void mon_set_move(Mon *m, int slot, int move) {
    if (slot < 0 || slot >= MON_MOVES) return;
    if (move < 0 || move > PC_MOVE_COUNT) return;
    m->moves[slot] = (uint8_t)move;
    m->pp[slot] = move ? pc_moves[move].pp : 0;
}

void mon_heal(Mon *m) {
    m->hp = m->stat[MON_HP];
    m->status = 0;
}

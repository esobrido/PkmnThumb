/*
 * mon/party.h — the trainer's team: six slots, in order.
 *
 * The original keeps wPartyCount plus six 48-byte mon structs at wPartyMons,
 * and the order is the player's (the one at index 0 leads). The port keeps the
 * same order and the same cap, and nothing that needs a save format yet:
 * adding puts a mon in the first free slot, removing shifts the rest down so
 * the team is never sparse.
 *
 * Pure: a struct in, no Mote, no globals, no allocation.
 */
#ifndef MON_PARTY_H
#define MON_PARTY_H

#include "mon/mon.h"

/* Six is the original's PARTY_LENGTH, not a choice. */
#define PARTY_MAX 6

typedef struct {
    Mon mon[PARTY_MAX];
    uint8_t count;
} Party;

/* An empty team. */
void party_init(Party *p);

/* Put `m` after the last member (a copy: the caller keeps its own). Returns the
 * slot it landed in, or -1 when the team is full. */
int party_add(Party *p, const Mon *m);

/* Drop the mon at index `i`; everyone after it moves down one slot. Returns 1
 * when it removed something, 0 for an index that held no mon. */
int party_remove(Party *p, int i);

/* Pokémon Center: every member healed. */
void party_heal(Party *p);

/* How many members can still fight, and the first of them (-1 if none). */
int party_alive(const Party *p);
int party_first_alive(const Party *p);

#endif

#include "mon/party.h"

void party_init(Party *p) {
    p->count = 0;
    for (int i = 0; i < PARTY_MAX; i++) {
        p->mon[i].species = 0;
        p->mon[i].hp = 0;
        p->mon[i].stat[MON_HP] = 0;
    }
}

int party_add(Party *p, const Mon *m) {
    if (p->count >= PARTY_MAX) return -1;
    p->mon[p->count] = *m;
    return p->count++;
}

int party_remove(Party *p, int i) {
    if (i < 0 || i >= p->count) return 0;
    for (int k = i; k + 1 < p->count; k++) p->mon[k] = p->mon[k + 1];
    p->count--;
    p->mon[p->count].species = 0;
    return 1;
}

void party_heal(Party *p) {
    for (int i = 0; i < p->count; i++) mon_heal(&p->mon[i]);
}

int party_alive(const Party *p) {
    int n = 0;
    for (int i = 0; i < p->count; i++)
        if (p->mon[i].hp > 0) n++;
    return n;
}

int party_first_alive(const Party *p) {
    for (int i = 0; i < p->count; i++)
        if (p->mon[i].hp > 0) return i;
    return -1;
}

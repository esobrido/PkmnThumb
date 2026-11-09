#include "bag.h"

void bag_init(PcBag *b) {
    b->count = 0;
    for (int i = 0; i < BAG_SLOTS; i++) {
        b->item[i] = 0xffff;
        b->qty[i] = 0;
    }
}

int bag_add(PcBag *b, uint16_t item, uint8_t qty) {
    if (!qty) return 1;
    for (int i = 0; i < b->count; i++) {
        if (b->item[i] != item) continue;
        /* 99 per stack is the GB's own cap (`MAX_ITEM_STACK`); a slot that is
         * already at it cannot take more, same as a full bag. */
        if (b->qty[i] + qty > 99) return 0;
        b->qty[i] = (uint8_t)(b->qty[i] + qty);
        return 1;
    }
    if (b->count >= BAG_SLOTS) return 0;
    b->item[b->count] = item;
    b->qty[b->count] = qty;
    b->count++;
    return 1;
}

uint8_t bag_count(const PcBag *b, uint16_t item) {
    for (int i = 0; i < b->count; i++)
        if (b->item[i] == item) return b->qty[i];
    return 0;
}

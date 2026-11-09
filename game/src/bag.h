/*
 * bag.h — what the player is carrying.
 *
 * The original's bag is a pocket list with quantities (`engine/items/bag.asm`),
 * and a hidden item is only "taken" once `giveitem` returns success: the flag is
 * set after the item is in, so a full bag leaves the cell readable. The port has
 * no bag screen yet (GAM-07), but the *giving* has to exist or a hidden item
 * would either vanish or be handed out forever, so this is the storage half:
 * item ids from the original's `constants/item_constants.asm` and a count each.
 *
 * Pure: no Mote, no globals, the caller owns the struct.
 */
#ifndef PC_BAG_H
#define PC_BAG_H

#include <stdint.h>

/* Pockets hold 20 kinds in the original (`BAG_CAPACITY`). */
#define BAG_SLOTS 20

typedef struct {
    uint8_t count;               /* slots in use, 0..BAG_SLOTS */
    uint16_t item[BAG_SLOTS];    /* original item ids, in the order found */
    uint8_t qty[BAG_SLOTS];
} PcBag;

/* Empty bag. */
void bag_init(PcBag *b);

/* Adds `qty` of `item`, stacking onto an existing slot. Returns 1 when it fit,
 * 0 when the bag is full (a new kind and no room), which is the "pocket is full"
 * branch of the original — the caller must then leave the world as it was. */
int bag_add(PcBag *b, uint16_t item, uint8_t qty);

/* How many of `item` the bag holds (0 when it has none). */
uint8_t bag_count(const PcBag *b, uint16_t item);

#endif

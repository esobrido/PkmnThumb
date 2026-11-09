/* bag — what the player is carrying.
 *
 * Only the giving half exists so far: a hidden item has to land somewhere, and
 * the flag that consumes the cell is only set when the item fitted. These cases
 * pin the two rules the original has: same kind stacks, a full bag refuses.
 */
#include <stdio.h>

#include "bag.h"

#include "test_util.h"

/* The ids the port's maps use, from constants/item_constants.asm. */
#define POTION 25
#define NUGGET 229

static void test_empty(void) {
    PcBag b;
    bag_init(&b);
    CHECK_EQ(b.count, 0);
    CHECK_EQ(bag_count(&b, POTION), 0);
}

static void test_new_slots(void) {
    PcBag b;
    bag_init(&b);
    CHECK_EQ(bag_add(&b, POTION, 1), 1);
    CHECK_EQ(b.count, 1);
    CHECK_EQ(b.item[0], POTION);
    CHECK_EQ(bag_count(&b, POTION), 1);
    CHECK_EQ(bag_add(&b, NUGGET, 1), 1);
    CHECK_EQ(b.count, 2);
    CHECK_EQ(b.item[1], NUGGET);
    CHECK_EQ(bag_count(&b, POTION), 1);   /* the first slot is untouched */
}

static void test_stack(void) {
    PcBag b;
    bag_init(&b);
    CHECK_EQ(bag_add(&b, POTION, 1), 1);
    CHECK_EQ(bag_add(&b, POTION, 1), 1);
    CHECK_EQ(b.count, 1);                 /* one slot, not two */
    CHECK_EQ(bag_count(&b, POTION), 2);
}

static void test_full(void) {
    PcBag b;
    bag_init(&b);
    for (int i = 0; i < BAG_SLOTS; i++)
        CHECK_EQ(bag_add(&b, (uint16_t)(100 + i), 1), 1);
    CHECK_EQ(b.count, BAG_SLOTS);
    CHECK_EQ(bag_add(&b, NUGGET, 1), 0);  /* no room for a new kind */
    CHECK_EQ(bag_count(&b, NUGGET), 0);
    CHECK_EQ(bag_add(&b, 100, 1), 1);     /* but a kind inside still stacks */
    CHECK_EQ(bag_count(&b, 100), 2);
}

static void test_stack_cap(void) {
    PcBag b;
    bag_init(&b);
    CHECK_EQ(bag_add(&b, POTION, 99), 1);
    CHECK_EQ(bag_add(&b, POTION, 1), 0);
    CHECK_EQ(bag_count(&b, POTION), 99);
}

static void test_nothing(void) {
    PcBag b;
    bag_init(&b);
    CHECK_EQ(bag_add(&b, POTION, 0), 1);
    CHECK_EQ(b.count, 0);
}

int main(void) {
    test_empty();
    test_new_slots();
    test_stack();
    test_full();
    test_stack_cap();
    test_nothing();
    return test_report("bag");
}

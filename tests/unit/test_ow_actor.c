/*
 * ow_actor: NPC facing, in-place spin animation and Y-sorted draw order.
 *
 * The interesting properties are the ones a screenshot cannot check cheaply:
 * standing NPCs never turn, spinners cycle through all four facings and come
 * back, the "random" spinner is deterministic (same seed, same sequence) so
 * frames stay reproducible, and the depth sort puts lower sprites last (drawn
 * on top) without shuffling equal y.
 */
#include "ow/actor.h"
#include "test_util.h"

int main(void) {
    OwActor a;

    /* a standing NPC keeps its facing forever */
    ow_actor_init(&a, 16, 16, OW_DIR_LEFT, OW_ANIM_STAND);
    for (int i = 0; i < 100; i++) CHECK_EQ(ow_actor_update(&a, 1), 0);
    CHECK_EQ(a.dir, OW_DIR_LEFT);
    CHECK_EQ(ow_actor_sheet_frame(&a), 4);
    CHECK_EQ(ow_actor_flip(&a), 0);

    /* counter-clockwise: one facing every OW_SPIN_FRAMES updates,
     * down -> right -> up -> left -> down (the original's table order) */
    ow_actor_init(&a, 16, 16, OW_DIR_DOWN, OW_ANIM_SPIN_CCW);
    CHECK_EQ(ow_actor_update(&a, OW_SPIN_FRAMES - 1), 0);
    CHECK_EQ(a.dir, OW_DIR_DOWN);
    CHECK_EQ(ow_actor_update(&a, 1), 1);
    CHECK_EQ(a.dir, OW_DIR_RIGHT);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_UP);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_LEFT);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_DOWN);          /* back to the start after 4 turns */

    /* clockwise is the mirror image */
    ow_actor_init(&a, 16, 16, OW_DIR_DOWN, OW_ANIM_SPIN_CW);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_LEFT);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_UP);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_RIGHT);
    ow_actor_update(&a, OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_DOWN);

    /* a burst of frames must not lose rotations: 4 turns in 4*OW_SPIN_FRAMES */
    ow_actor_init(&a, 16, 16, OW_DIR_DOWN, OW_ANIM_SPIN_CCW);
    ow_actor_update(&a, 4 * OW_SPIN_FRAMES);
    CHECK_EQ(a.dir, OW_DIR_DOWN);

    /* the random spinner is reproducible: two actors with the same position and
     * kind take the same decisions, and both use all four facings over time */
    OwActor b, c;
    ow_actor_init(&b, 32, 48, OW_DIR_DOWN, OW_ANIM_SPIN_RANDOM);
    ow_actor_init(&c, 32, 48, OW_DIR_DOWN, OW_ANIM_SPIN_RANDOM);
    int seen[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 64; i++) {
        ow_actor_update(&b, 1);
        ow_actor_update(&c, 1);
        CHECK_EQ(b.dir, c.dir);
        if (b.dir >= 0 && b.dir < 4) seen[b.dir]++;
    }
    for (int d = 0; d < 4; d++) CHECK(seen[d] > 0);

    /* different positions get different seeds, so the town does not spin in
     * lockstep */
    ow_actor_init(&b, 0, 0, OW_DIR_DOWN, OW_ANIM_SPIN_RANDOM);
    ow_actor_init(&c, 16, 0, OW_DIR_DOWN, OW_ANIM_SPIN_RANDOM);
    int differ = 0;
    for (int i = 0; i < 32; i++) {
        ow_actor_update(&b, 1);
        ow_actor_update(&c, 1);
        if (b.dir != c.dir) differ = 1;
    }
    CHECK_EQ(differ, 1);

    /* right-facing actors reuse the left frames mirrored */
    ow_actor_init(&a, 0, 0, OW_DIR_RIGHT, OW_ANIM_STAND);
    CHECK_EQ(ow_actor_sheet_frame(&a), 4);
    CHECK_EQ(ow_actor_flip(&a), 1);
    ow_actor_init(&a, 0, 0, OW_DIR_UP, OW_ANIM_STAND);
    CHECK_EQ(ow_actor_sheet_frame(&a), 2);

    /* Y-sort: lower on the map is drawn last (on top) */
    {
        const int feet[4] = { 100, 20, 60, 20 };
        int order[4];
        ow_actor_depth_sort(feet, 4, order);
        CHECK_EQ(order[0], 1);     /* y=20 */
        CHECK_EQ(order[1], 3);     /* y=20, ties keep data order */
        CHECK_EQ(order[2], 2);     /* y=60 */
        CHECK_EQ(order[3], 0);     /* y=100 */
    }

    /* zero and one sprite are not special cases */
    {
        const int feet1[1] = { 5 };
        int order[1] = { 7 };
        ow_actor_depth_sort(feet1, 0, order);
        CHECK_EQ(order[0], 7);     /* untouched */
        ow_actor_depth_sort(feet1, 1, order);
        CHECK_EQ(order[0], 0);
    }

    /* already sorted input stays put, reversed input is flipped */
    {
        const int asc[3] = { 10, 20, 30 };
        int order[3];
        ow_actor_depth_sort(asc, 3, order);
        CHECK_EQ(order[0], 0);
        CHECK_EQ(order[1], 1);
        CHECK_EQ(order[2], 2);
        ow_actor_depth_sort(asc, 3, order);   /* reusable, no state kept */
        CHECK_EQ(order[2], 2);
    }

    return test_report("test_ow_actor");
}

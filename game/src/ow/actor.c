#include "ow/actor.h"

void ow_actor_init(OwActor *a, int px, int py, int dir, int anim) {
    a->px = px;
    a->py = py;
    a->dir = dir;
    a->anim = anim;
    a->step_left = 0;
    a->walk_timer = 0;
    a->spin_timer = 0;
    /* Deterministic seed from the position: no time, no global RNG, so a
     * screenshot of frame N is always the same frame. */
    a->rng = (uint32_t)px * 2654435761u + (uint32_t)py * 40503u + 1u;
}

static uint32_t next_rand(OwActor *a) {
    a->rng = a->rng * 1103515245u + 12345u;
    return a->rng >> 16;
}

int ow_actor_update(OwActor *a, int frames) {
    if (a->anim == OW_ANIM_STAND || frames <= 0) return 0;

    int turned = 0;
    a->spin_timer += frames;
    while (a->spin_timer >= OW_SPIN_FRAMES) {
        a->spin_timer -= OW_SPIN_FRAMES;
        switch (a->anim) {
            case OW_ANIM_SPIN_CCW:
                /* the original's .Directions table: DOWN, RIGHT, UP, LEFT */
                a->dir = a->dir == OW_DIR_DOWN ? OW_DIR_RIGHT
                       : a->dir == OW_DIR_RIGHT ? OW_DIR_UP
                       : a->dir == OW_DIR_UP ? OW_DIR_LEFT
                       : OW_DIR_DOWN;
                break;
            case OW_ANIM_SPIN_CW:
                a->dir = a->dir == OW_DIR_DOWN ? OW_DIR_LEFT
                       : a->dir == OW_DIR_LEFT ? OW_DIR_UP
                       : a->dir == OW_DIR_UP ? OW_DIR_RIGHT
                       : OW_DIR_DOWN;
                break;
            default:     /* OW_ANIM_SPIN_RANDOM: any of the four, independently */
                a->dir = (int)(next_rand(a) & 3);
                break;
        }
        turned = 1;
    }
    return turned;
}

int ow_actor_sheet_frame(const OwActor *a) {
    return ow_sheet_frame(a->dir, a->step_left > 0, a->walk_timer);
}

int ow_actor_flip(const OwActor *a) {
    return ow_sheet_flip(a->dir);
}

void ow_actor_depth_sort(const int *feet_y, int n, int *order) {
    /* Insertion sort: n here is a handful of on-screen sprites, and it is stable
     * without extra memory. */
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        int idx = order[i];
        int y = feet_y[idx];
        int j = i - 1;
        while (j >= 0 && feet_y[order[j]] > y) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = idx;
    }
}

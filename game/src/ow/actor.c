#include "ow/actor.h"

void ow_actor_init(OwActor *a, int px, int py, int dir, int anim) {
    a->px = px;
    a->py = py;
    a->dir = dir;
    a->anim = anim;
    a->step_left = 0;
    a->step_frames = 0;
    a->step_acc = 0;
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

uint16_t ow_actor_rand(OwActor *a) {
    return (uint16_t)next_rand(a);
}

void ow_actor_start_step(OwActor *a, int dir, int frames) {
    if (frames <= 0) return;
    a->dir = dir;
    a->step_left = OW_STEP_PX;
    a->step_frames = frames;
    a->step_acc = 0;
    a->spin_timer = 0;      /* a spinner resumes with a full turn, not a burst */
}

int ow_actor_walking(const OwActor *a) {
    return a->step_left > 0;
}

/* One step, exactly like ow/player.c: the pixels come from the frames walked,
 * so a slow step (2 frames per pixel) and a fast one (2 px per frame) land on the
 * same cell without the caller knowing about it. */
static void walk_step(OwActor *a, int frames) {
    if (a->step_left <= 0 || a->step_frames <= 0) return;
    a->step_acc += frames;
    int walked = (a->step_acc * OW_STEP_PX) / a->step_frames;
    if (walked > OW_STEP_PX) walked = OW_STEP_PX;
    int adv = walked - (OW_STEP_PX - a->step_left);
    if (adv <= 0) return;
    switch (a->dir) {
        case OW_DIR_UP:    a->py -= adv; break;
        case OW_DIR_DOWN:  a->py += adv; break;
        case OW_DIR_LEFT:  a->px -= adv; break;
        default:           a->px += adv; break;
    }
    a->step_left -= adv;
    a->walk_timer += adv;
    if (a->step_left == 0) a->step_acc = 0;
}

int ow_actor_update(OwActor *a, int frames) {
    if (frames <= 0) return 0;
    walk_step(a, frames);
    if (a->anim == OW_ANIM_STAND || a->step_left > 0) return 0;

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

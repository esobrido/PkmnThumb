#include "ow/move.h"

#include "gen/move.h"

void ow_move_start(OwMove *m, const uint8_t *code, uint16_t len) {
    m->code = code;
    m->len = len;
    m->pos = 0;
}

void ow_move_reset(OwMove *m) {
    m->code = 0;
    m->len = 0;
    m->pos = 0;
}

int ow_move_running(const OwMove *m) {
    return m->code != 0;
}

void ow_move_stop(OwMove *m) {
    ow_move_reset(m);
}

/* The direction a `step_*`/`turn_step_*` byte walks towards. The original's
 * numbering is four blocks of four (down, up, left, right) for turns, slow
 * steps, steps and the rest, which is why the low two bits are the facing. */
static int dir_of(uint8_t op) {
    switch (op & 3) {
        case 0:  return OW_DIR_DOWN;
        case 1:  return OW_DIR_UP;
        case 2:  return OW_DIR_LEFT;
        default: return OW_DIR_RIGHT;
    }
}

/* Frames a step of this kind takes, or 0 if it is not a step. */
static int step_frames(uint8_t op) {
    if (op >= PCM_SLOW_STEP_DOWN && op <= PCM_SLOW_STEP_RIGHT) return OW_STEP_SLOW;
    if (op >= PCM_STEP_DOWN && op <= PCM_STEP_RIGHT) return OW_STEP_NORMAL;
    if (op >= PCM_BIG_STEP_DOWN && op <= PCM_BIG_STEP_RIGHT) return OW_STEP_NORMAL;
    if (op >= PCM_SLOW_SLIDE_STEP_DOWN && op <= PCM_FAST_SLIDE_STEP_RIGHT) {
        /* slides keep their facing and go one cell, slower or faster */
        if (op <= PCM_SLOW_SLIDE_STEP_RIGHT) return OW_STEP_SLOW;
        if (op <= PCM_SLIDE_STEP_RIGHT) return OW_STEP_NORMAL;
        return OW_STEP_FAST;
    }
    if (op >= PCM_SLOW_JUMP_STEP_DOWN && op <= PCM_SLOW_JUMP_STEP_RIGHT) return OW_STEP_SLOW;
    if (op >= PCM_JUMP_STEP_DOWN && op <= PCM_JUMP_STEP_RIGHT) return OW_STEP_NORMAL;
    if (op >= PCM_FAST_JUMP_STEP_DOWN && op <= PCM_FAST_JUMP_STEP_RIGHT) return OW_STEP_FAST;
    if (op >= PCM_RUN_STEP_DOWN && op <= PCM_RUN_STEP_RIGHT) return OW_STEP_FAST;
    if (op >= PCM_FAST_STEP_DOWN && op <= PCM_FAST_STEP_RIGHT) return OW_STEP_FAST;
    return 0;
}

static int turn_dir(uint8_t op) {
    if (op <= PCM_TURN_HEAD_RIGHT) return dir_of(op);   /* $00-$03 */
    if (op >= PCM_TURN_STEP_DOWN && op <= PCM_TURN_STEP_RIGHT) return dir_of(op);
    if (op >= PCM_TURN_AWAY_DOWN && op <= PCM_TURN_AWAY_RIGHT) return dir_of(op);
    if (op >= PCM_TURN_IN_DOWN && op <= PCM_TURN_IN_RIGHT) return dir_of(op);
    if (op >= PCM_TURN_WATERFALL_DOWN && op <= PCM_TURN_WATERFALL_RIGHT) return dir_of(op);
    return OW_DIR_NONE;
}

OwMoveKind ow_move_next(OwMove *m, OwMoveOut *out) {
    out->kind = OW_MOVE_END;
    out->dir = OW_DIR_NONE;
    out->frames = 0;
    out->b = 0;
    out->opcode = 0;
    if (!m->code) return OW_MOVE_END;

    while (m->pos < m->len) {
        uint8_t op = m->code[m->pos++];

        if (op == PCM_STEP_END) {
            ow_move_reset(m);
            return OW_MOVE_END;
        }

        int frames = step_frames(op);
        int turn = turn_dir(op);
        if (frames) {
            out->kind = OW_MOVE_STEP;
            out->dir = (uint8_t)dir_of(op);
            out->frames = (uint8_t)frames;
            return OW_MOVE_STEP;
        }
        if (turn != OW_DIR_NONE) {
            out->kind = OW_MOVE_TURN;
            out->dir = (uint8_t)turn;
            return OW_MOVE_TURN;
        }

        /* `step_sleep_1`..`step_sleep_8` are one byte each; `step_sleep` takes
         * the duration as its own byte. */
        if (op >= PCM_STEP_SLEEP_1 && op <= PCM_STEP_SLEEP_8) {
            out->kind = OW_MOVE_WAIT;
            out->frames = (uint8_t)(op - PCM_STEP_SLEEP_1 + 1);
            return OW_MOVE_WAIT;
        }
        if (op == PCM_STEP_SLEEP) {
            if (m->pos >= m->len) break;
            out->kind = OW_MOVE_WAIT;
            out->frames = m->code[m->pos++];
            return OW_MOVE_WAIT;
        }

        if (op == PCM_SHOW_OBJECT || op == PCM_HIDE_OBJECT) {
            out->kind = OW_MOVE_SHOW;
            out->b = (uint8_t)(op == PCM_SHOW_OBJECT);
            return OW_MOVE_SHOW;
        }

        /* Facing fixes and the sliding flags only matter to the "obey the D-pad"
         * movement type, which no ported map uses: they are consumed here so a
         * stream that carries them still plays. */
        if (op == PCM_FIX_FACING || op == PCM_REMOVE_FIXED_FACING ||
                op == PCM_SET_SLIDING || op == PCM_REMOVE_SLIDING) {
            continue;
        }

        out->kind = OW_MOVE_UNSUPPORTED;
        out->opcode = op;
        ow_move_reset(m);
        return OW_MOVE_UNSUPPORTED;
    }

    /* Running out of stream is the extractor's job to prevent; if it happens,
     * end the movement instead of reading the next script as steps. */
    ow_move_reset(m);
    return OW_MOVE_END;
}

/* ---- the SPRITEMOVEDATA_WALK_* patterns --------------------------------- */

void ow_wander_init(OwWander *w) {
    w->timer = 0;
}

int ow_wander_next(OwWander *w, OwActor *a, uint8_t move) {
    if (w->timer) {
        w->timer--;
        return OW_DIR_NONE;
    }
    if (ow_actor_walking(a)) return OW_DIR_NONE;

    uint16_t r = ow_actor_rand(a);
    int dir = OW_DIR_NONE;
    switch (move) {
        case PC_SPRITEMOVEDATA_WANDER:
            dir = (int)(r & 3);
            break;
        case PC_SPRITEMOVEDATA_WALK_UP_DOWN:
            dir = (r & 1) ? OW_DIR_UP : OW_DIR_DOWN;
            break;
        case PC_SPRITEMOVEDATA_WALK_LEFT_RIGHT:
            dir = (r & 1) ? OW_DIR_LEFT : OW_DIR_RIGHT;
            break;
        default:
            return OW_DIR_NONE;   /* standing, spinning, scripted... */
    }
    /* One decision per walk, not per frame: the original draws a random
     * duration (up to 254 frames) before the object walks again. */
    w->timer = (uint8_t)(16 + (ow_actor_rand(a) & 0x3f));
    return dir;
}

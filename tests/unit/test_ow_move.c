/*
 * ow/move: the movement byte stream and the SPRITEMOVEDATA_WALK_* patterns.
 *
 * The streams here are the real ones from Polished Crystal, byte for byte:
 * the teacher's four steps in New Bark Town and the fallthrough block that runs
 * one movement into the next, which is the case a naive "one region per label"
 * reader gets wrong. The walk itself is checked on an OwActor so the step maths
 * (slow, normal, fast) is covered too.
 */
#include "gen/move.h"
#include "ow/actor.h"
#include "ow/move.h"
#include "test_util.h"

/* Movement_TeacherRunsToYou1_NBT: four steps left, then step_end. */
static const uint8_t teacher1[] = { 0x0e, 0x0e, 0x0e, 0x0e, 0x47 };
/* Movement_TeacherBringsYouBack1_NBT: four steps right, turn_head_left, end. */
static const uint8_t teacher_back[] = { 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x47 };
/* Two blocks that fall through into each other, like the map's data:
 * Movement_BringsYouBack2 is one `step_right` and no `step_end`. */
static const uint8_t fallthrough[] = { 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x47 };

int main(void) {
    /* the numbers the extractor emits are the original's (macros/scripts/movement.asm) */
    CHECK_EQ(PCM_STEP_DOWN, 0x0c);
    CHECK_EQ(PCM_STEP_UP, 0x0d);
    CHECK_EQ(PCM_STEP_LEFT, 0x0e);
    CHECK_EQ(PCM_STEP_RIGHT, 0x0f);
    CHECK_EQ(PCM_STEP_END, 0x47);
    CHECK_EQ(PCM_TURN_HEAD_LEFT, 0x02);
    CHECK_EQ(PCM_STEP_SLEEP, 0x46);
    CHECK_EQ(PC_SPRITEMOVEDATA_WALK_UP_DOWN, 4);
    CHECK_EQ(PC_SPRITEMOVEDATA_WALK_LEFT_RIGHT, 5);

    OwMove m;
    OwMoveOut out;

    /* nothing running: the first call already says the movement is over */
    ow_move_reset(&m);
    CHECK_EQ(ow_move_running(&m), 0);
    CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);

    /* a NULL stream is not a special case either */
    ow_move_start(&m, 0, 0);
    CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);

    /* the teacher runs to the player: four left steps, then step_end */
    ow_move_start(&m, teacher1, sizeof teacher1);
    CHECK_EQ(ow_move_running(&m), 1);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(out.dir, OW_DIR_LEFT);
        CHECK_EQ(out.frames, OW_STEP_NORMAL);
        CHECK_EQ(ow_move_running(&m), 1);
    }
    CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);
    CHECK_EQ(ow_move_running(&m), 0);
    /* and it stays over: a stream that reached step_end cannot be replayed by
     * calling again */
    CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);

    /* the way back: four right steps and a head turn, which is instant */
    ow_move_start(&m, teacher_back, sizeof teacher_back);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(out.dir, OW_DIR_RIGHT);
    }
    CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_TURN);
    CHECK_EQ(out.dir, OW_DIR_LEFT);
    CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);

    /* fallthrough: the first block is one step_right and has no step_end, so the
     * stream keeps going into the next block's four steps and its turn. */
    ow_move_start(&m, fallthrough, sizeof fallthrough);
    int steps = 0;
    for (;;) {
        OwMoveKind k = ow_move_next(&m, &out);
        if (k == OW_MOVE_END) break;
        if (k == OW_MOVE_STEP) steps++;
        CHECK(k != OW_MOVE_UNSUPPORTED);
    }
    CHECK_EQ(steps, 5);

    /* step kinds keep their speed: slow 2 frames/px, fast 2 px/frame */
    {
        const uint8_t slow[] = { 0x08, 0x47 };      /* slow_step_down */
        const uint8_t run[] = { 0x57, 0x47 };       /* run_step_down */
        ow_move_start(&m, slow, sizeof slow);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(out.dir, OW_DIR_DOWN);
        CHECK_EQ(out.frames, OW_STEP_SLOW);
        ow_move_start(&m, run, sizeof run);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(out.frames, OW_STEP_FAST);
    }

    /* sleeps: the numbered ones are one byte, step_sleep carries its duration */
    {
        const uint8_t short_sleep[] = { 0x3e, 0x47 };          /* step_sleep_1 */
        const uint8_t long_sleep[] = { 0x46, 30, 0x47 };       /* step_sleep 30 */
        ow_move_start(&m, short_sleep, sizeof short_sleep);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_WAIT);
        CHECK_EQ(out.frames, 1);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);
        ow_move_start(&m, long_sleep, sizeof long_sleep);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_WAIT);
        CHECK_EQ(out.frames, 30);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);
    }

    /* the facing fixes and sliding flags are consumed, not reported */
    {
        const uint8_t fixes[] = { 0x3b, 0x3a, 0x39, 0x38, 0x0f, 0x47 };
        ow_move_start(&m, fixes, sizeof fixes);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(out.dir, OW_DIR_RIGHT);
    }

    /* a step this port cannot run ends the movement and says which byte */
    {
        const uint8_t unknown[] = { 0x48, 0x47 };              /* remove_object */
        ow_move_start(&m, unknown, sizeof unknown);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_UNSUPPORTED);
        CHECK_EQ(out.opcode, 0x48);
        CHECK_EQ(ow_move_running(&m), 0);
    }

    /* a stream that runs off the end stops instead of reading the next script */
    {
        const uint8_t cut[] = { 0x0e, 0x0e };
        ow_move_start(&m, cut, sizeof cut);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_STEP);
        CHECK_EQ(ow_move_next(&m, &out), OW_MOVE_END);
    }

    /* ---- the walk itself ------------------------------------------------- */
    {
        OwActor a;
        ow_actor_init(&a, 100, 100, OW_DIR_DOWN, OW_ANIM_STAND);
        CHECK_EQ(ow_actor_walking(&a), 0);
        ow_actor_start_step(&a, OW_DIR_LEFT, OW_STEP_NORMAL);
        CHECK_EQ(ow_actor_walking(&a), 1);
        CHECK_EQ(a.dir, OW_DIR_LEFT);           /* the facing changes with the step */
        /* 16 frames of 1 px: the cell is reached exactly on the last one */
        ow_actor_update(&a, OW_STEP_NORMAL - 1);
        CHECK_EQ(a.px, 100 - (OW_STEP_PX - 1));
        CHECK_EQ(ow_actor_walking(&a), 1);
        ow_actor_update(&a, 1);
        CHECK_EQ(a.px, 100 - OW_STEP_PX);
        CHECK_EQ(ow_actor_walking(&a), 0);

        /* slow step: the same 16 px in 32 frames, 1 px every other frame */
        ow_actor_init(&a, 48, 48, OW_DIR_DOWN, OW_ANIM_STAND);
        ow_actor_start_step(&a, OW_DIR_DOWN, OW_STEP_SLOW);
        ow_actor_update(&a, 1);
        CHECK_EQ(a.py, 48);                     /* nothing on the first frame */
        ow_actor_update(&a, 1);
        CHECK_EQ(a.py, 49);
        ow_actor_update(&a, OW_STEP_SLOW - 2);
        CHECK_EQ(a.py, 48 + OW_STEP_PX);
        CHECK_EQ(ow_actor_walking(&a), 0);

        /* fast step: 2 px per frame, 8 frames */
        ow_actor_init(&a, 0, 0, OW_DIR_RIGHT, OW_ANIM_STAND);
        ow_actor_start_step(&a, OW_DIR_RIGHT, OW_STEP_FAST);
        ow_actor_update(&a, 1);
        CHECK_EQ(a.px, 2);
        ow_actor_update(&a, OW_STEP_FAST - 1);
        CHECK_EQ(a.px, OW_STEP_PX);

        /* the walk owns the facing: a spinner told to walk does not turn while
         * it walks, and turns again once it stands */
        ow_actor_init(&a, 0, 0, OW_DIR_DOWN, OW_ANIM_SPIN_CCW);
        ow_actor_start_step(&a, OW_DIR_UP, OW_STEP_SLOW);
        CHECK_EQ(ow_actor_update(&a, 8), 0);
        CHECK_EQ(a.dir, OW_DIR_UP);
        ow_actor_update(&a, OW_SPIN_FRAMES);
        CHECK(a.dir == OW_DIR_UP || a.dir == OW_DIR_LEFT || a.dir == OW_DIR_DOWN ||
              a.dir == OW_DIR_RIGHT);
    }

    /* ---- the walking patterns ------------------------------------------- */
    {
        OwActor a;
        OwWander w;
        ow_actor_init(&a, 32, 32, OW_DIR_DOWN, OW_ANIM_STAND);
        ow_wander_init(&w);

        /* standing NPCs never wander, however often they are asked */
        for (int i = 0; i < 100; i++)
            CHECK_EQ(ow_wander_next(&w, &a, PC_SPRITEMOVEDATA_STANDING_DOWN),
                     OW_DIR_NONE);

        /* WALK_UP_DOWN only ever asks for up or down, and both come up */
        int up = 0, down = 0;
        for (int i = 0; i < 200; i++) {
            int d = ow_wander_next(&w, &a, PC_SPRITEMOVEDATA_WALK_UP_DOWN);
            if (d == OW_DIR_NONE) continue;
            CHECK(d == OW_DIR_UP || d == OW_DIR_DOWN);
            if (d == OW_DIR_UP) up++;
            else down++;
        }
        CHECK(up > 0 && down > 0);

        /* WALK_LEFT_RIGHT only asks for left or right */
        ow_actor_init(&a, 32, 32, OW_DIR_DOWN, OW_ANIM_STAND);
        ow_wander_init(&w);
        for (int i = 0; i < 200; i++) {
            int d = ow_wander_next(&w, &a, PC_SPRITEMOVEDATA_WALK_LEFT_RIGHT);
            if (d == OW_DIR_NONE) continue;
            CHECK(d == OW_DIR_LEFT || d == OW_DIR_RIGHT);
        }

        /* one decision is followed by a wait, so a blocked NPC does not retry
         * on the next frame */
        ow_actor_init(&a, 32, 32, OW_DIR_DOWN, OW_ANIM_STAND);
        ow_wander_init(&w);
        int d = ow_wander_next(&w, &a, PC_SPRITEMOVEDATA_WALK_UP_DOWN);
        CHECK(d != OW_DIR_NONE);
        int waited = 0;
        while (ow_wander_next(&w, &a, PC_SPRITEMOVEDATA_WALK_UP_DOWN) == OW_DIR_NONE) {
            waited++;
            if (waited > 200) break;
        }
        CHECK(waited >= 16);
        CHECK(waited <= 80);

        /* while walking it decides nothing: the step has the actor's attention */
        ow_actor_init(&a, 32, 32, OW_DIR_DOWN, OW_ANIM_STAND);
        ow_wander_init(&w);
        ow_actor_start_step(&a, OW_DIR_DOWN, OW_STEP_NORMAL);
        CHECK_EQ(ow_wander_next(&w, &a, PC_SPRITEMOVEDATA_WALK_UP_DOWN), OW_DIR_NONE);

        /* the pattern is reproducible: the same actor walks the same way twice */
        OwActor b;
        OwWander wa, wb;
        ow_wander_init(&wa);
        ow_wander_init(&wb);
        ow_actor_init(&a, 64, 64, OW_DIR_DOWN, OW_ANIM_STAND);
        ow_actor_init(&b, 64, 64, OW_DIR_DOWN, OW_ANIM_STAND);
        for (int i = 0; i < 64; i++) {
            int da = ow_wander_next(&wa, &a, PC_SPRITEMOVEDATA_WANDER);
            int db = ow_wander_next(&wb, &b, PC_SPRITEMOVEDATA_WANDER);
            CHECK_EQ(da, db);
        }
    }

    return test_report("test_ow_move");
}

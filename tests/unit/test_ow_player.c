/* ow_player: the walking state machine (steps, blocking, turning, animation). */
#include "ow/player.h"
#include "test_util.h"

/* 4x3 blocks. Row 0 is { free, wall, free, free }: the whole block (1,0) blocks.
 * Everything else is free. 0xF = all four quadrants blocked. */
static const uint8_t blocks[4 * 3] = { 0, 1, 0, 0,
                                      0, 0, 0, 0,
                                      0, 0, 0, 0 };
static const uint8_t mt_tiles[2][16] = { { 0 }, { 0 } };
static const uint8_t mt_coll[2] = { 0x0, 0xF };
static const uint8_t mt_pal[2 * 16] = { 0 };

static OwMap make_map(void) {
    OwMap m;
    m.w = 4;
    m.h = 3;
    m.blocks = blocks;
    m.mt_tiles = mt_tiles;
    m.mt_coll = mt_coll;
    m.mt_pal = mt_pal;
    m.cells = 0;
    return m;
}

/* One NPC standing on the cell east of (1,2) — the cells layer is (w*2)x(h*2),
 * so (2,2) is index 2*8 + 2. NPCs block without being in the tileset. */
static const uint8_t npc_cells[8 * 6] = { 0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0 };

/* Frames per half-turn with Turning Speed = Slow (the default); 2 means Fast. */
#define SLOW 4

/* Walk `frames` frames holding `dir`, one pixel per frame (60 fps). */
static void walk(OwPlayer *p, const OwMap *m, int dir, int frames) {
    for (int i = 0; i < frames; i++) ow_player_update(p, m, dir, 1, 0, SLOW);
}

/* The same, with the running shoes on: the step covers two pixels per frame. */
static void run(OwPlayer *p, const OwMap *m, int dir, int frames) {
    for (int i = 0; i < frames; i++) ow_player_update(p, m, dir, 1, 1, SLOW);
}

/* Hold `dir` for `frames` frames with the given turning speed, walking. */
static void turn(OwPlayer *p, const OwMap *m, int dir, int frames, int speed) {
    for (int i = 0; i < frames; i++) ow_player_update(p, m, dir, 1, 0, speed);
}

int main(void) {
    OwMap m = make_map();
    OwPlayer p;

    /* a step is 16 px and takes 16 frames at 1 px/frame. Starting by pressing
     * the direction the player already faces must walk, not freeze. */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    walk(&p, &m, OW_DIR_DOWN, 8);
    CHECK_EQ(p.py, 16 + 8);
    CHECK_EQ(p.step_left, 8);
    walk(&p, &m, OW_DIR_DOWN, 8);
    CHECK_EQ(p.py, 16 + 16);
    CHECK_EQ(p.px, 16);

    /* holding the direction keeps walking: the step restarts on arrival */
    CHECK_EQ(p.step_left, OW_WALK_FRAMES);

    /* releasing mid-step finishes the step and stops aligned to the grid */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    walk(&p, &m, OW_DIR_DOWN, 5);
    CHECK_EQ(p.py, 21);
    walk(&p, &m, OW_DIR_NONE, 20);      /* more frames than needed */
    CHECK_EQ(p.py, 32);
    CHECK_EQ(p.py % OW_STEP_PX, 0);
    CHECK_EQ(p.step_left, 0);

    /* just_arrived marks the one frame a step finishes: it is what the warps
     * read as "the step onto this cell has ended". Not before, and not while
     * the next step of the same held direction is already running. */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    walk(&p, &m, OW_DIR_DOWN, 15);
    CHECK_EQ(p.just_arrived, 0);        /* 15 px into a 16 px step */
    walk(&p, &m, OW_DIR_DOWN, 1);       /* the 16th px lands on the cell */
    CHECK_EQ(p.py, 32);
    CHECK_EQ(p.just_arrived, 1);
    ow_player_update(&p, &m, OW_DIR_DOWN, 1, 0, SLOW);
    CHECK_EQ(p.just_arrived, 0);        /* the flag lasts one frame only */
    CHECK_EQ(p.step_left, OW_WALK_FRAMES - 1);   /* and the next step began */
    /* a bump is not an arrival */
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);
    walk(&p, &m, OW_DIR_RIGHT, 8);
    CHECK_EQ(p.bumping, 1);
    CHECK_EQ(p.just_arrived, 0);

    /* a wall stops the step and leaves the player where it was */
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);   /* block (0,1) -> next is wall */
    CHECK_EQ(ow_player_try_step(&p, &m, OW_DIR_RIGHT), 0);
    walk(&p, &m, OW_DIR_RIGHT, 40);
    CHECK_EQ(p.px, 16);
    CHECK_EQ(p.step_left, 0);
    CHECK_EQ(p.dir, OW_DIR_RIGHT);      /* it still turns to face the wall */

    /* pushing a wall walks in place: the sprite keeps cycling its two walk
     * frames (the original's ._WalkInPlace) while the feet stay put, and the
     * standing frame is back the frame the button is released */
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);
    walk(&p, &m, OW_DIR_RIGHT, 4);
    CHECK_EQ(p.bumping, 1);
    CHECK_EQ(p.px, 16);
    CHECK_EQ(p.py, 16);
    CHECK_EQ(p.step_left, 0);           /* a bump is not a step */
    CHECK_EQ(ow_player_sheet_frame(&p), 4);   /* right: left frames, first pose */
    walk(&p, &m, OW_DIR_RIGHT, 4);      /* 8 frames of pushing */
    CHECK_EQ(ow_player_sheet_frame(&p), 5);   /* legs swapped, still no movement */
    CHECK_EQ(p.px, 16);
    walk(&p, &m, OW_DIR_NONE, 1);
    CHECK_EQ(p.bumping, 0);
    CHECK_EQ(ow_player_sheet_frame(&p), 4);   /* standing again, immediately */

    /* the running shoes do not speed the push up: the GB counts frames there,
     * not pixels, so 4 frames of running into a wall is still the first pose */
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);
    run(&p, &m, OW_DIR_RIGHT, 4);
    CHECK_EQ(p.bumping, 1);
    CHECK_EQ(ow_player_sheet_frame(&p), 4);

    /* an NPC blocks like a wall, and pushing one also animates in place */
    OwMap mn = make_map();
    mn.cells = npc_cells;
    ow_player_init(&p, 16, 32, OW_DIR_RIGHT);   /* NPC on cell (2,2) */
    walk(&p, &mn, OW_DIR_RIGHT, 8);
    CHECK_EQ(p.px, 16);
    CHECK_EQ(p.bumping, 1);
    CHECK_EQ(p.dir, OW_DIR_RIGHT);

    /* a turn cancels the push: turning to face elsewhere shows the standing
     * frame, not the walk cycle of the direction being left */
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);
    walk(&p, &m, OW_DIR_RIGHT, 4);              /* bumping east */
    walk(&p, &m, OW_DIR_LEFT, 1);               /* and now ask for the west */
    CHECK_EQ(p.bumping, 0);
    CHECK_EQ(ow_player_sheet_frame(&p), 4);     /* still facing right, standing */

    /* turning in place: the direction only flips halfway, and no step starts
     * until the turn is over. Slow = 4 frames per half, so 8 in total. */
    ow_player_init(&p, 48, 48, OW_DIR_DOWN);
    turn(&p, &m, OW_DIR_LEFT, 1, SLOW);
    CHECK_EQ(p.dir, OW_DIR_DOWN);            /* still facing down */
    CHECK_EQ(p.step_left, 0);                /* and not walking */
    turn(&p, &m, OW_DIR_LEFT, 2, SLOW);      /* frames 2-3 */
    CHECK_EQ(p.dir, OW_DIR_DOWN);
    turn(&p, &m, OW_DIR_LEFT, 1, SLOW);      /* frame 4: halfway */
    CHECK_EQ(p.dir, OW_DIR_LEFT);
    CHECK_EQ(p.px, 48);
    CHECK_EQ(p.py, 48);
    CHECK_EQ(p.step_left, 0);
    CHECK_EQ(ow_player_sheet_frame(&p), 4);  /* the standing frame of the new facing */
    turn(&p, &m, OW_DIR_LEFT, 4, SLOW);      /* frames 5-8: the second half */
    CHECK_EQ(p.turn_left, 0);
    CHECK_EQ(p.px, 48);                      /* the first step starts on frame 9... */
    CHECK_EQ(p.step_left, 0);
    walk(&p, &m, OW_DIR_LEFT, 4);
    CHECK_EQ(p.px, 48 - 4);                  /* ...and it does walk now */

    /* Fast halves the turn: 2 + 2 frames instead of 8 */
    ow_player_init(&p, 48, 48, OW_DIR_DOWN);
    turn(&p, &m, OW_DIR_LEFT, 2, 2);
    CHECK_EQ(p.dir, OW_DIR_LEFT);            /* already flipped */
    CHECK_EQ(p.step_left, 0);
    turn(&p, &m, OW_DIR_LEFT, 2, 2);
    CHECK_EQ(p.turn_left, 0);
    CHECK_EQ(p.px, 48);
    walk(&p, &m, OW_DIR_LEFT, 1);
    CHECK_EQ(p.px, 48 - 1);                  /* walking 2 frames into the step */
    CHECK_EQ(p.step_left, OW_WALK_FRAMES - 1);

    /* a tap turns without taking a step: the turn finishes on its own */
    ow_player_init(&p, 48, 48, OW_DIR_DOWN);
    turn(&p, &m, OW_DIR_LEFT, 1, SLOW);
    walk(&p, &m, OW_DIR_NONE, 12);
    CHECK_EQ(p.dir, OW_DIR_LEFT);
    CHECK_EQ(p.px, 48);
    CHECK_EQ(p.py, 48);
    CHECK_EQ(p.step_left, 0);

    /* input is ignored mid-step: a step in progress cannot be redirected */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    walk(&p, &m, OW_DIR_DOWN, 8);
    walk(&p, &m, OW_DIR_RIGHT, 1);            /* asked for mid-step */
    CHECK_EQ(p.dir, OW_DIR_DOWN);
    CHECK_EQ(p.px, 16);                      /* no sideways drift off the grid */
    walk(&p, &m, OW_DIR_DOWN, 8);
    CHECK_EQ(p.py, 32 + 1);                 /* still going down, one px into the next */
    walk(&p, &m, OW_DIR_NONE, 20);          /* released: it finishes the cell */
    CHECK_EQ(p.py, 48);
    CHECK_EQ(p.px, 16);
    CHECK_EQ(p.px % OW_STEP_PX, 0);
    CHECK_EQ(p.py % OW_STEP_PX, 0);

    /* asking for the facing you already have walks straight away, no turn */
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);
    walk(&p, &m, OW_DIR_RIGHT, 1);            /* block (0,1) is a wall, so it stands */
    CHECK_EQ(p.step_left, 0);
    CHECK_EQ(p.turn_left, 0);

    /* the map edge blocks like a wall */
    ow_player_init(&p, 0, 16, OW_DIR_LEFT);
    walk(&p, &m, OW_DIR_LEFT, 40);
    CHECK_EQ(p.px, 0);

    /* animation: standing vs. walking frames, per facing */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    CHECK_EQ(ow_player_sheet_frame(&p), 0);   /* down, standing */
    ow_player_update(&p, &m, OW_DIR_DOWN, 1, 0, SLOW);
    CHECK_EQ(ow_player_sheet_frame(&p), 0);   /* down, first half of the step */
    ow_player_update(&p, &m, OW_DIR_DOWN, 8, 0, SLOW);
    CHECK_EQ(ow_player_sheet_frame(&p), 1);   /* down, walk frame */

    ow_player_init(&p, 16, 16, OW_DIR_UP);
    CHECK_EQ(ow_player_sheet_frame(&p), 2);
    ow_player_init(&p, 16, 16, OW_DIR_LEFT);
    CHECK_EQ(ow_player_sheet_frame(&p), 4);
    CHECK_EQ(ow_player_flip(&p), 0);
    ow_player_init(&p, 16, 16, OW_DIR_RIGHT);
    CHECK_EQ(ow_player_sheet_frame(&p), 4);   /* right reuses the left frames */
    CHECK_EQ(ow_player_flip(&p), 1);

    /* running: 2 px per frame, so a whole 16 px cell takes OW_RUN_FRAMES */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    run(&p, &m, OW_DIR_DOWN, OW_RUN_FRAMES / 2);
    CHECK_EQ(p.py, 16 + OW_STEP_PX / 2);      /* half a cell in 4 frames */
    CHECK_EQ(p.step_left, OW_STEP_PX / 2);
    CHECK_EQ(ow_player_sheet_frame(&p), 1);   /* legs already moved on */
    run(&p, &m, OW_DIR_DOWN, OW_RUN_FRAMES / 2);
    CHECK_EQ(p.py, 16 + OW_STEP_PX);          /* the whole cell in 8 frames */
    CHECK_EQ(p.step_left, OW_WALK_FRAMES);    /* and, still held, the next began */
    CHECK_EQ(ow_player_sheet_frame(&p), 0);

    /* running twice as far as walking in the same number of frames */
    OwPlayer slow, fast;
    ow_player_init(&slow, 16, 16, OW_DIR_DOWN);
    ow_player_init(&fast, 16, 16, OW_DIR_DOWN);
    walk(&slow, &m, OW_DIR_DOWN, 16);
    run(&fast, &m, OW_DIR_DOWN, 16);
    CHECK_EQ(slow.py, 16 + OW_STEP_PX);
    CHECK_EQ(fast.py, 16 + 2 * OW_STEP_PX);
    CHECK_EQ(fast.px, 16);

    /* a step never overshoots its cell: the remainder is dropped, not carried */
    ow_player_init(&p, 16, 16, OW_DIR_DOWN);
    ow_player_update(&p, &m, OW_DIR_DOWN, 1, 1, SLOW);
    CHECK_EQ(p.py, 18);
    CHECK_EQ(p.step_left, 14);

    /* a wall stops a run exactly like a walk */
    ow_player_init(&p, 0, 16, OW_DIR_LEFT);
    run(&p, &m, OW_DIR_LEFT, 40);
    CHECK_EQ(p.px, 0);

    return test_report("test_ow_player");
}

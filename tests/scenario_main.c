/*
 * Scenario runner — behaviour tests a human can write without reading the code.
 *
 * A scenario is a text file: a grid of walkable/blocked 16x16 cells, where the
 * player starts and faces, the buttons held for how many frames, and the state
 * expected at the end. See docs/07-conventions.md for the format and
 * tests/scenarios/ for examples.
 *
 *   tests/build/scenario tests/scenarios/walk_east.txt
 *
 * `hold` walks (1 px per frame); `run` is the same step with the running shoes
 * (2 px per frame, so a cell takes 8 frames instead of 16).
 *
 * Exit status is 0 when the final state matches, 1 otherwise (and it prints the
 * state it actually reached, so fixing a scenario is a copy-paste).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ow/map.h"
#include "ow/player.h"

#define MAX_DIM 64
#define MAX_STEPS 64

static const char *dir_name(int d) {
    switch (d) {
        case OW_DIR_DOWN:  return "down";
        case OW_DIR_UP:    return "up";
        case OW_DIR_LEFT:  return "left";
        case OW_DIR_RIGHT: return "right";
        default:           return "none";
    }
}

static int dir_of(const char *s) {
    if (!strcmp(s, "down"))  return OW_DIR_DOWN;
    if (!strcmp(s, "up"))    return OW_DIR_UP;
    if (!strcmp(s, "left"))  return OW_DIR_LEFT;
    if (!strcmp(s, "right")) return OW_DIR_RIGHT;
    return OW_DIR_NONE;
}

/* Frames per half-turn at the default Turning Speed of Slow (the original's
 * .GetTurningSpeed): a turn between facings costs 2 x 4 = 8 frames. */
enum { SCEN_TURN_FRAMES = 4 };

typedef struct {
    int dir;
    int frames;
    int run;        /* holding B is the caller's business: this is the speed */
} ScenarioStep;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: scenario <file.txt>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "scenario: cannot open %s\n", argv[1]);
        return 2;
    }

    static char cells[MAX_DIM * MAX_DIM];
    int w = 0, h = 0, rows_read = 0;
    int start_tx = 0, start_ty = 0, start_dir = OW_DIR_DOWN;
    int expect_px = 0, expect_py = 0, expect_dir = OW_DIR_DOWN, have_expect = 0;
    ScenarioStep steps[MAX_STEPS];
    int nsteps = 0;
    int line_no = 0;

    char line[512];
    while (fgets(line, sizeof line, f)) {
        line_no++;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == '\r' || *s == 0) continue;

        char word[32] = { 0 };
        if (sscanf(s, "%31s", word) != 1) continue;

        if (!strcmp(word, "kind")) continue;          /* reserved: only "unit" today */

        if (!strcmp(word, "map")) {
            if (sscanf(s, "%*s %dx%d", &w, &h) != 2 || w <= 0 || h <= 0 ||
                w > MAX_DIM || h > MAX_DIM) {
                fprintf(stderr, "%s:%d: bad map size\n", argv[1], line_no);
                return 2;
            }
            continue;
        }

        if (!strcmp(word, "start")) {
            char d[16] = { 0 };
            if (sscanf(s, "%*s %d %d %15s", &start_tx, &start_ty, d) < 2) {
                fprintf(stderr, "%s:%d: bad start\n", argv[1], line_no);
                return 2;
            }
            if (d[0]) start_dir = dir_of(d);
            continue;
        }

        if (!strcmp(word, "hold") || !strcmp(word, "run")) {
            char d[16] = { 0 };
            int frames = 0;
            if (sscanf(s, "%*s %15s %d", d, &frames) != 2) {
                fprintf(stderr, "%s:%d: bad hold\n", argv[1], line_no);
                return 2;
            }
            if (nsteps >= MAX_STEPS) {
                fprintf(stderr, "%s:%d: too many steps\n", argv[1], line_no);
                return 2;
            }
            steps[nsteps].dir = dir_of(d);
            steps[nsteps].frames = frames;
            steps[nsteps].run = word[0] == 'r';
            nsteps++;
            continue;
        }

        if (!strcmp(word, "expect")) {
            char d[16] = { 0 };
            if (sscanf(s, "%*s %d %d %15s", &expect_px, &expect_py, d) < 2) {
                fprintf(stderr, "%s:%d: bad expect\n", argv[1], line_no);
                return 2;
            }
            if (d[0]) expect_dir = dir_of(d);
            have_expect = 1;
            continue;
        }

        /* A line of the grid: '.' free, '#' blocked, one char per 16x16 cell. */
        int n = 0;
        for (const char *c = s; *c && *c != '\n' && *c != '\r'; c++) {
            if (*c == ' ' || *c == '\t') continue;
            if (*c != '.' && *c != '#') {
                fprintf(stderr, "%s:%d: unexpected '%c' (use . or #)\n",
                        argv[1], line_no, *c);
                return 2;
            }
            if (rows_read >= h || n >= w) {
                fprintf(stderr, "%s:%d: grid row too long\n", argv[1], line_no);
                return 2;
            }
            cells[rows_read * w + n] = (*c == '#');
            n++;
        }
        if (n > 0) rows_read++;
    }
    fclose(f);

    if (w <= 0 || h <= 0 || rows_read != h) {
        fprintf(stderr, "%s: map needs %d rows of %d cells, got %d\n",
                argv[1], h, w, rows_read);
        return 2;
    }

    /* Turn the grid into the engine's two-level structure: 2x2 cells make one
     * 32x32 block, and a metatile id encodes which of its quadrants block. */
    static uint8_t blocks[(MAX_DIM / 2 + 1) * (MAX_DIM / 2 + 1)];
    static uint8_t mt_tiles[16][16];
    static uint8_t mt_pal[16 * 16];
    uint8_t mt_coll[16];
    int nbx = (w + 1) / 2, nby = (h + 1) / 2;

    for (int i = 0; i < 16; i++) mt_coll[i] = (uint8_t)i;

    for (int by = 0; by < nby; by++) {
        for (int bx = 0; bx < nbx; bx++) {
            int mask = 0;
            for (int q = 0; q < 4; q++) {
                int cx = bx * 2 + (q & 1), cy = by * 2 + (q >> 1);
                int blocked = 1;               /* off-grid counts as blocked */
                if (cx < w && cy < h) blocked = cells[cy * w + cx];
                if (blocked) mask |= 1 << q;
            }
            blocks[by * nbx + bx] = (uint8_t)mask;
        }
    }

    OwMap map = { nbx, nby, blocks, (const uint8_t (*)[16])mt_tiles, mt_coll, mt_pal, 0 };
    OwPlayer p;
    ow_player_init(&p, start_tx * OW_STEP_PX, start_ty * OW_STEP_PX, start_dir);

    for (int i = 0; i < nsteps; i++)
        for (int k = 0; k < steps[i].frames; k++)
            ow_player_update(&p, &map, steps[i].dir, 1, steps[i].run, SCEN_TURN_FRAMES);

    /* Positions are in 16 px cells, the same unit as the map grid. */
    int got_tx = p.px / OW_STEP_PX, got_ty = p.py / OW_STEP_PX;

    if (!have_expect) {
        printf("state %d %d %s\n", got_tx, got_ty, dir_name(p.dir));
        return 0;
    }

    if (got_tx != expect_px || got_ty != expect_py || p.dir != expect_dir) {
        printf("FAIL %s: expected %d %d %s, got %d %d %s\n", argv[1],
               expect_px, expect_py, dir_name(expect_dir),
               got_tx, got_ty, dir_name(p.dir));
        return 1;
    }
    printf("PASS %s\n", argv[1]);
    return 0;
}

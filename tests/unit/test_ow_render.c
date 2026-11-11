/* ow_render: the banded map raster (palette per tile, band clipping, camera). */
#include "ow/render.h"
#include "test_util.h"

#define SENTINEL 0xDEAD

/* One block. Its first tile has a single pixel of colour index 1 at (0,0) and
 * colour 0 everywhere else, so we can see exactly where it lands. */
static const uint8_t blocks[1] = { 0 };
static const uint8_t mt_tiles[1][16] = { { 0 } };
static const uint8_t mt_coll[1] = { 0 };
static const uint8_t mt_pal[16] = { 0 };
static const uint8_t tiles[1][16] = {
    { 0x80, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};
static const uint16_t pals[1][4] = { { 0x1111, 0x2222, 0x3333, 0x4444 } };

static OwMap make_map(void) {
    OwMap m;
    m.w = 1;
    m.h = 1;
    m.blocks = blocks;
    m.mt_tiles = mt_tiles;
    m.mt_coll = mt_coll;
    m.mt_grass = 0;
    m.mt_pal = mt_pal;
    m.cells = 0;
    return m;
}

static void fill(uint16_t *fb, int n, uint16_t v) {
    for (int i = 0; i < n; i++) fb[i] = v;
}

int main(void) {
    OwMap m = make_map();
    uint16_t fb[16 * 16];

    /* full band: colour index 0 of the tile's palette fills the tile body */
    fill(fb, 16 * 16, SENTINEL);
    ow_render_band(fb, 16, 16, 0, 16, &m, tiles, pals, 0, 0);
    CHECK_EQ(fb[0], 0x2222);          /* index 1 at the tile's (0,0) */
    CHECK_EQ(fb[1], 0x1111);          /* index 0 elsewhere */
    CHECK_EQ(fb[16], 0x1111);         /* row 1 */
    CHECK_EQ(fb[15 * 16 + 15], 0x1111);

    /* band clipping: only the requested rows are written */
    fill(fb, 16 * 16, SENTINEL);
    ow_render_band(fb, 16, 16, 4, 6, &m, tiles, pals, 0, 0);
    CHECK_EQ(fb[3 * 16 + 1], SENTINEL);   /* row 3 untouched */
    CHECK_EQ(fb[4 * 16 + 1], 0x1111);     /* row 4 written */
    CHECK_EQ(fb[5 * 16 + 1], 0x1111);     /* row 5 written */
    CHECK_EQ(fb[6 * 16 + 1], SENTINEL);   /* row 6 untouched */

    /* an empty or out-of-range band does nothing and does not crash */
    fill(fb, 16 * 16, SENTINEL);
    ow_render_band(fb, 16, 16, 6, 6, &m, tiles, pals, 0, 0);
    ow_render_band(fb, 16, 16, 20, 40, &m, tiles, pals, 0, 0);
    CHECK_EQ(fb[0], SENTINEL);
    CHECK_EQ(fb[15 * 16 + 15], SENTINEL);

    /* scrolling by one 8x8 tile: the window now starts at world x=8, so every
     * screen column is the *next* tile; the tile's (0,0) pixel moves with it */
    fill(fb, 16 * 16, SENTINEL);
    ow_render_band(fb, 16, 16, 0, 2, &m, tiles, pals, 8, 0);
    CHECK_EQ(fb[0], 0x2222);          /* world (8,0) = tile (0,0) */
    CHECK_EQ(fb[1], 0x1111);
    CHECK_EQ(fb[8], 0x2222);          /* world (16,0) = next tile's (0,0) */
    CHECK_EQ(fb[9], 0x1111);

    return test_report("test_ow_render");
}

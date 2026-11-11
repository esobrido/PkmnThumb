/* ow_map: block lookup and per-quadrant collision. */
#include "ow/map.h"
#include "test_util.h"

/* Two blocks side by side, one row tall.
 *   block 0: free everywhere
 *   block 1: only its bottom-right 16x16 quadrant is blocked
 * Quadrant bits are TL=0 TR=1 BL=2 BR=3. */
static const uint8_t blocks[2] = { 0, 1 };
static const uint8_t mt_tiles[2][16] = { { 0 }, { 0 } };
static const uint8_t mt_coll[2] = { 0x0, 0x8 };
/* block 0: its top-right cell is tall grass (a walkable collision code) */
static const uint8_t mt_grass[2] = { 0x2, 0x0 };
static const uint8_t mt_pal[2 * 16] = { 0 };

static OwMap make_map(void) {
    OwMap m;
    m.w = 2;
    m.h = 1;
    m.blocks = blocks;
    m.mt_tiles = mt_tiles;
    m.mt_coll = mt_coll;
    m.mt_grass = mt_grass;
    m.mt_pal = mt_pal;
    m.cells = 0;
    return m;
}

int main(void) {
    OwMap m = make_map();

    /* grass reads the collision code table, not the blocked mask: block 0's
     * top-right quadrant is walkable tall grass, block 1 is not grass at all */
    CHECK_EQ(ow_cell_grass(&m, 1, 0), 1);
    CHECK_EQ(ow_cell_grass(&m, 0, 0), 0);
    CHECK_EQ(ow_cell_grass(&m, 1, 1), 0);
    CHECK_EQ(ow_cell_grass(&m, 3, 0), 0);
    CHECK_EQ(ow_cell_grass(&m, -1, 0), 0);
    CHECK_EQ(ow_cell_grass(&m, 4, 0), 0);
    CHECK_EQ(ow_px_grass(&m, 16, 0), 1);
    CHECK_EQ(ow_px_grass(&m, 0, 0), 0);
    /* a map without the table never reports grass */
    OwMap ng = make_map();
    ng.mt_grass = 0;
    CHECK_EQ(ow_cell_grass(&ng, 1, 0), 0);

    /* block ids, with out-of-bounds reading as the maps' border block (0) */
    CHECK_EQ(ow_block_at(&m, 0, 0), 0);
    CHECK_EQ(ow_block_at(&m, 1, 0), 1);
    CHECK_EQ(ow_block_at(&m, -1, 0), 0);
    CHECK_EQ(ow_block_at(&m, 2, 0), 0);
    CHECK_EQ(ow_block_at(&m, 0, 1), 0);

    /* the four 16x16 cells of block 0 are free */
    CHECK_EQ(ow_cell_blocked(&m, 0, 0), 0);
    CHECK_EQ(ow_cell_blocked(&m, 1, 0), 0);
    CHECK_EQ(ow_cell_blocked(&m, 0, 1), 0);
    CHECK_EQ(ow_cell_blocked(&m, 1, 1), 0);

    /* block 1: only BR is blocked (bit 3) */
    CHECK_EQ(ow_cell_blocked(&m, 2, 0), 0);   /* TL */
    CHECK_EQ(ow_cell_blocked(&m, 3, 0), 0);   /* TR */
    CHECK_EQ(ow_cell_blocked(&m, 2, 1), 0);   /* BL */
    CHECK_EQ(ow_cell_blocked(&m, 3, 1), 1);   /* BR */

    /* outside the map counts as blocked, so the player cannot walk off it */
    CHECK_EQ(ow_cell_blocked(&m, -1, 0), 1);
    CHECK_EQ(ow_cell_blocked(&m, 4, 0), 1);
    CHECK_EQ(ow_cell_blocked(&m, 0, 2), 1);

    /* A second blocker layer: NPCs are not in the tileset collision, but their
     * cell is not walkable either. Layer is (w*2) x (h*2) cells. */
    static const uint8_t cells[4 * 2] = { 0, 1, 0, 0,  0, 0, 0, 0 };
    m.cells = cells;
    CHECK_EQ(ow_cell_blocked(&m, 1, 0), 1);   /* NPC standing there */
    CHECK_EQ(ow_cell_blocked(&m, 0, 0), 0);
    CHECK_EQ(ow_cell_blocked(&m, 0, 1), 0);
    CHECK_EQ(ow_px_blocked(&m, 16, 0), 1);    /* same cell in pixels */
    m.cells = 0;
    CHECK_EQ(ow_cell_blocked(&m, 1, 0), 0);   /* clearing the layer clears it */

    /* pixel queries: (48, 16) is cell (3, 1), the blocked quadrant */
    CHECK_EQ(ow_px_blocked(&m, 0, 0), 0);
    CHECK_EQ(ow_px_blocked(&m, 48, 16), 1);
    CHECK_EQ(ow_px_blocked(&m, 32, 0), 0);

    return test_report("test_ow_map");
}

/* ow_connect: which edge you leave through, and where you come out. */
#include "ow/connect.h"
#include "ow/map.h"      /* OW_STEP_PX */
#include "test_util.h"

/* Two maps side by side: a 4x3-block (8x6-cell) map west of a 3x2-block (6x4-cell)
 * one, with a north/south pair as well. Offsets are in blocks, as the original's
 * `connection` rows are, and land the player `2 * offset` cells along the seam. */
static const PcConnection east[1] = {
    /* dir,          map, offset */
    { OW_DIR_RIGHT,   1,   2 },
};
static const PcConnection west[1] = {
    { OW_DIR_LEFT,    0,  -3 },
};
static const PcConnection no_edge[2] = {
    { OW_DIR_UP,      1,   0 },
    { OW_DIR_DOWN,   -1,   0 },   /* shut: the other side is not in the build */
};

int main(void) {
    /* ---- looking one up -------------------------------------------------- */
    CHECK(ow_connect_at(east, 1, OW_DIR_RIGHT) == &east[0]);
    CHECK_EQ(ow_connect_at(east, 1, OW_DIR_LEFT), 0);
    CHECK_EQ(ow_connect_at(east, 1, OW_DIR_NONE), 0);
    CHECK_EQ(ow_connect_at(0, 3, OW_DIR_RIGHT), 0);
    CHECK_EQ(ow_connect_at(east, 0, OW_DIR_RIGHT), 0);
    CHECK(ow_connect_at(no_edge, 2, OW_DIR_DOWN) == &no_edge[1]);

    /* ---- the edge cell --------------------------------------------------- */
    /* a 4x3-block map is 8x6 walking cells */
    CHECK_EQ(ow_connect_on_edge(0, 3, 4, 3, OW_DIR_LEFT), 1);
    CHECK_EQ(ow_connect_on_edge(1, 3, 4, 3, OW_DIR_LEFT), 0);
    CHECK_EQ(ow_connect_on_edge(7, 3, 4, 3, OW_DIR_RIGHT), 1);
    CHECK_EQ(ow_connect_on_edge(6, 3, 4, 3, OW_DIR_RIGHT), 0);
    CHECK_EQ(ow_connect_on_edge(3, 0, 4, 3, OW_DIR_UP), 1);
    CHECK_EQ(ow_connect_on_edge(3, 1, 4, 3, OW_DIR_UP), 0);
    CHECK_EQ(ow_connect_on_edge(3, 5, 4, 3, OW_DIR_DOWN), 1);
    CHECK_EQ(ow_connect_on_edge(3, 4, 4, 3, OW_DIR_DOWN), 0);
    /* the edge is direction-specific: the left column is not the right one */
    CHECK_EQ(ow_connect_on_edge(0, 3, 4, 3, OW_DIR_RIGHT), 0);
    CHECK_EQ(ow_connect_on_edge(3, 0, 4, 3, OW_DIR_DOWN), 0);
    /* outside the map, or no direction at all */
    CHECK_EQ(ow_connect_on_edge(-1, 3, 4, 3, OW_DIR_LEFT), 0);
    CHECK_EQ(ow_connect_on_edge(0, 6, 4, 3, OW_DIR_LEFT), 0);
    CHECK_EQ(ow_connect_on_edge(0, 3, 4, 3, OW_DIR_NONE), 0);

    /* ---- where you land -------------------------------------------------- */
    int px, py;

    /* east with offset 2: the destination is 3x2 blocks = 6x4 cells, so you
     * arrive on its left column, two blocks (4 cells) further up than you left:
     * the offset is subtracted, which is the original's `_y = offset * -2` */
    CHECK_EQ(ow_connect_land(&east[0], 3, 2, 7, 5, &px, &py), 1);
    CHECK_EQ(px, 0);
    CHECK_EQ(py, 1 * OW_STEP_PX);

    /* west with offset -3: a negative offset shifts the other way, so six cells
     * down; the destination here is 4x6 blocks to have room for that */
    CHECK_EQ(ow_connect_land(&west[0], 4, 6, 0, 1, &px, &py), 1);
    CHECK_EQ(px, 7 * OW_STEP_PX);
    CHECK_EQ(py, 7 * OW_STEP_PX);

    /* a zero offset keeps the coordinate exactly: the two seams are flush */
    CHECK_EQ(ow_connect_land(&no_edge[0], 3, 2, 3, 0, &px, &py), 1);
    CHECK_EQ(px, 3 * OW_STEP_PX);
    CHECK_EQ(py, 3 * OW_STEP_PX);     /* 2 blocks tall = 4 cells, so the last is 3 */

    /* north/south shift along x, not y */
    CHECK_EQ(ow_connect_land(&no_edge[0], 5, 4, 4, 0, &px, &py), 1);
    CHECK_EQ(px, 4 * OW_STEP_PX);
    CHECK_EQ(py, 7 * OW_STEP_PX);

    /* a shut connection is not a way out */
    CHECK_EQ(ow_connect_land(&no_edge[1], 3, 2, 1, 5, &px, &py), 0);
    CHECK_EQ(ow_connect_land(0, 3, 2, 1, 5, &px, &py), 0);

    /* an offset that lands outside the destination is refused, so the caller
     * leaves the edge solid rather than dropping the player off the map */
    CHECK_EQ(ow_connect_land(&east[0], 3, 2, 7, 0, &px, &py), 0);   /* y = -4 */
    CHECK_EQ(ow_connect_land(&west[0], 4, 6, 0, 6, &px, &py), 0);   /* y = 12 */
    CHECK_EQ(ow_connect_land(&east[0], 0, 2, 7, 1, &px, &py), 0);   /* empty map */
    CHECK_EQ(ow_connect_land(&east[0], 3, 0, 7, 1, &px, &py), 0);

    return test_report("test_ow_connect");
}

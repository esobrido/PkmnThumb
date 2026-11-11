/* ow_camera: centring on the player and clamping to the map. */
#include "ow/camera.h"
#include "test_util.h"

static const uint8_t blocks[12 * 9] = { 0 };
static const uint8_t mt_tiles[1][16] = { { 0 } };
static const uint8_t mt_coll[1] = { 0 };
static const uint8_t mt_pal[16] = { 0 };

static OwMap make_map(int w, int h) {
    OwMap m;
    m.w = w;
    m.h = h;
    m.blocks = blocks;
    m.mt_tiles = mt_tiles;
    m.mt_coll = mt_coll;
    m.mt_grass = 0;
    m.mt_pal = mt_pal;
    m.cells = 0;
    return m;
}

#define SCREEN 128

int main(void) {
    OwCamera c;
    OwMap big = make_map(12, 9);              /* 384x288 px, like New Bark Town */

    /* clamped at the top-left corner */
    ow_camera_follow(&c, &big, 0, 0, SCREEN);
    CHECK_EQ(c.x, 0);
    CHECK_EQ(c.y, 0);

    /* free in the middle: cam = player + half a cell - half a screen */
    ow_camera_follow(&c, &big, 192, 96, SCREEN);
    CHECK_EQ(c.x, 192 + 8 - 64);
    CHECK_EQ(c.y, 96 + 8 - 64);

    /* clamped at the bottom-right corner: map size - screen */
    ow_camera_follow(&c, &big, 368, 272, SCREEN);
    CHECK_EQ(c.x, 384 - SCREEN);
    CHECK_EQ(c.y, 288 - SCREEN);

    /* a map smaller than the screen shows from its origin */
    OwMap small = make_map(1, 1);             /* 32x32 px */
    ow_camera_follow(&c, &small, 16, 16, SCREEN);
    CHECK_EQ(c.x, 0);
    CHECK_EQ(c.y, 0);

    /* one axis clamped, the other not */
    ow_camera_follow(&c, &big, 300, 8, SCREEN);
    CHECK_EQ(c.x, 300 + 8 - 64);
    CHECK_EQ(c.y, 0);

    return test_report("test_ow_camera");
}

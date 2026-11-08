/* gfx_sprite: 2bpp expansion, transparency, mirroring, frames and clipping. */
#include "gfx_sprite.h"
#include "test_util.h"

#define SENTINEL 0x0101
#define FB_W 16
#define FB_H 16

static const uint16_t pal[4] = { 0, 0xAAAA, 0xBBBB, 0xCCCC };

static void fill(uint16_t *fb, int n, uint16_t v) {
    for (int i = 0; i < n; i++) fb[i] = v;
}

int main(void) {
    /* ---- expansion: MSB is the leftmost pixel, bit 0 = low plane ---------- */
    uint16_t tile[64];
    /* row 0: low plane 0b10000001 -> pixels 0 and 7; high plane 0b00000010 ->
     * pixel 6 takes index 2 */
    const uint8_t src[16] = { 0x81, 0x02, 0, 0, 0, 0, 0, 0,
                              0, 0, 0, 0, 0, 0, 0, 0 };
    sprite_expand_tile(tile, src, pal);
    CHECK_EQ(tile[0], pal[1]);
    CHECK_EQ(tile[1], pal[0]);
    CHECK_EQ(tile[6], pal[2]);
    CHECK_EQ(tile[7], pal[1]);
    CHECK_EQ(tile[8], pal[0]);          /* row 1 is blank */

    /* ---- a two-frame sheet -------------------------------------------------
     * Frame 0: one pixel at the top-left of the whole 16x16 frame, and one at
     * the top-left of its second 8x8 tile (x = 8). Frame 1: the same spot as
     * frame 0's first pixel, in another colour, so frame selection is visible. */
    static uint16_t sheet[2 * SPR_FRAME_TILES * SPR_TILE_PX2];
    fill(sheet, 2 * SPR_FRAME_TILES * SPR_TILE_PX2, 0);
    sheet[0] = 0x5555;                                     /* frame 0, (0,0) */
    sheet[1 * SPR_TILE_PX2] = 0x7777;                      /* frame 0, (8,0) */
    sheet[SPR_FRAME_TILES * SPR_TILE_PX2] = 0x6666;        /* frame 1, (0,0) */

    uint16_t fb[FB_W * FB_H];

    /* drawing honours transparency: only set pixels are written */
    fill(fb, FB_W * FB_H, SENTINEL);
    sprite_draw_frame(fb, FB_W, FB_H, sheet, 0, 4, 4, 0);
    CHECK_EQ(fb[4 * FB_W + 4], 0x5555);
    CHECK_EQ(fb[4 * FB_W + 12], 0x7777);    /* second tile column, same row */
    CHECK_EQ(fb[4 * FB_W + 5], SENTINEL);
    CHECK_EQ(fb[0], SENTINEL);

    /* frame selection reads the matching block of 4 tiles */
    fill(fb, FB_W * FB_H, SENTINEL);
    sprite_draw_frame(fb, FB_W, FB_H, sheet, 1, 0, 0, 0);
    CHECK_EQ(fb[0], 0x6666);

    /* mirroring is across the whole 16 px frame, so the left pixel of the
     * first tile column lands on the right edge — and the second column's
     * pixel lands 8 px from the right, not in place */
    fill(fb, FB_W * FB_H, SENTINEL);
    sprite_draw_frame(fb, FB_W, FB_H, sheet, 0, 0, 0, 1);
    CHECK_EQ(fb[15], 0x5555);
    CHECK_EQ(fb[7], 0x7777);
    CHECK_EQ(fb[0], SENTINEL);

    /* clipping: parked fully off-screen, nothing is written and nothing breaks */
    fill(fb, FB_W * FB_H, SENTINEL);
    sprite_draw_frame(fb, FB_W, FB_H, sheet, 0, 40, 40, 0);
    CHECK_EQ(fb[0], SENTINEL);
    CHECK_EQ(fb[FB_W * FB_H - 1], SENTINEL);
    fill(fb, FB_W * FB_H, SENTINEL);
    sprite_draw_frame(fb, FB_W, FB_H, sheet, 0, -20, 0, 0);
    CHECK_EQ(fb[0], SENTINEL);

    /* straddling an edge draws only the visible part: at x = -8 the frame's
     * second tile column is the half that shows */
    fill(fb, FB_W * FB_H, SENTINEL);
    sprite_draw_frame(fb, FB_W, FB_H, sheet, 0, -8, 4, 0);
    CHECK_EQ(fb[4 * FB_W + 0], 0x7777);     /* sprite (8,0) -> screen (0,4) */
    CHECK_EQ(fb[4 * FB_W + 8], SENTINEL);   /* frame's left pixel is clipped */

    return test_report("test_gfx_sprite");
}

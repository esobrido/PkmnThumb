/* ow_fade: the white blend the door transition draws over the framebuffer. */
#include "ow/fade.h"
#include "test_util.h"

/* RGB565, so the channels are 5, 6 and 5 bits: 0x0000 black, 0xFFFF white. */
static uint16_t ch_r(uint16_t c) { return (c >> 11) & 0x1F; }
static uint16_t ch_g(uint16_t c) { return (c >> 5) & 0x3F; }
static uint16_t ch_b(uint16_t c) { return c & 0x1F; }

int main(void) {
    uint16_t fb[8];

    /* level 0 is no transition at all: every pixel untouched, black and white
     * alike, and no pixel count is assumed */
    for (int i = 0; i < 8; i++) fb[i] = (uint16_t)(i * 1000);
    uint16_t before[8];
    for (int i = 0; i < 8; i++) before[i] = fb[i];
    ow_fade_apply(fb, 8, 0);
    for (int i = 0; i < 8; i++) CHECK_EQ(fb[i], before[i]);

    /* the last frame is the white screen the map is swapped under */
    for (int i = 0; i < 8; i++) fb[i] = 0x0000;
    ow_fade_apply(fb, 8, OW_FADE_STEPS);
    for (int i = 0; i < 8; i++) CHECK_EQ(fb[i], 0xFFFF);
    /* ...and past the end it stays white rather than wrapping back to black */
    ow_fade_apply(fb, 8, OW_FADE_STEPS + 5);
    for (int i = 0; i < 8; i++) CHECK_EQ(fb[i], 0xFFFF);
    ow_fade_apply(fb, 8, 99);
    CHECK_EQ(fb[0], 0xFFFF);

    /* a negative level must not darken anything either */
    for (int i = 0; i < 8; i++) fb[i] = 0x1234;
    ow_fade_apply(fb, 8, -3);
    CHECK_EQ(fb[0], 0x1234);

    /* every channel moves towards its own maximum, proportionally: from black,
     * half way is half of 31, 63 and 31 in that order */
    for (int i = 0; i < 8; i++) fb[i] = 0x0000;
    ow_fade_apply(fb, 8, OW_FADE_STEPS / 2);
    CHECK_EQ(ch_r(fb[0]), 16);          /* (31 * 5 + 5) / 10 = 16, rounded */
    CHECK_EQ(ch_g(fb[0]), 32);          /* (63 * 5 + 5) / 10 = 32 */
    CHECK_EQ(ch_b(fb[0]), 16);
    CHECK(ch_r(fb[0]) != 0x1F);         /* white, but not all the way */
    CHECK(ch_g(fb[0]) != 0x3F);

    /* from a bright colour every channel still only ever rises: the fade keeps
     * each pixel's ordering instead of flattening it to grey */
    for (int i = 0; i < 8; i++) fb[i] = 0xF800;   /* pure red */
    ow_fade_apply(fb, 8, 1);
    CHECK(ch_r(fb[0]) >= 0x1F);         /* red was already full, stays full */
    CHECK(ch_g(fb[0]) > 0);             /* green and blue are pulled up */
    CHECK(ch_b(fb[0]) > 0);
    CHECK(ch_g(fb[0]) < 0x3F);          /* but nowhere near white yet */
    CHECK(ch_b(fb[0]) < 0x1F);

    /* levels are monotone: 1 is darker than 5, which is darker than 9 */
    uint16_t l1, l5, l9;
    fb[0] = 0x0000; ow_fade_apply(fb, 1, 1); l1 = fb[0];
    fb[0] = 0x0000; ow_fade_apply(fb, 1, 5); l5 = fb[0];
    fb[0] = 0x0000; ow_fade_apply(fb, 1, 9); l9 = fb[0];
    CHECK(l1 < l5);
    CHECK(l5 < l9);
    CHECK(l1 > 0);

    /* only the first `pixels` entries are touched: the band in the middle of the
     * buffer keeps its colours while the fade is running */
    for (int i = 0; i < 8; i++) fb[i] = 0x0000;
    ow_fade_apply(fb, 3, OW_FADE_STEPS);
    CHECK_EQ(fb[0], 0xFFFF);
    CHECK_EQ(fb[2], 0xFFFF);
    CHECK_EQ(fb[3], 0x0000);
    CHECK_EQ(fb[7], 0x0000);
    /* a zero or negative pixel count is a no-op, not a crash */
    fb[0] = 0x0000;
    ow_fade_apply(fb, 0, OW_FADE_STEPS);
    CHECK_EQ(fb[0], 0x0000);

    return test_report("test_ow_fade");
}

/* ui/frame — the shared window box.
 *
 * Every screen in the port is drawn in one of these, so the two things that can
 * go wrong are worth pinning: the frame is exactly one pixel, and drawing is
 * clipped to the band the banded renderer asked for.
 */
#include <stdio.h>
#include <string.h>

#include "ui/frame.h"

#include "test_util.h"

#define W 128
#define H 128

static uint16_t fb[W * H];

#define INK 0x1111
#define PAPER 0x2222

static void clear(void) {
    for (int i = 0; i < W * H; i++) fb[i] = 0xdead;
}

static void test_box(void) {
    clear();
    ui_frame_box(fb, W, 0, H, 8, 4, 40, 24, INK, PAPER);

    CHECK_EQ(fb[4 * W + 8], INK);                 /* top-left corner */
    CHECK_EQ(fb[4 * W + 47], INK);                /* top-right */
    CHECK_EQ(fb[27 * W + 8], INK);                /* bottom-left */
    CHECK_EQ(fb[27 * W + 47], INK);               /* bottom-right */
    CHECK_EQ(fb[4 * W + 20], INK);                /* top edge between corners */
    CHECK_EQ(fb[27 * W + 20], INK);
    CHECK_EQ(fb[10 * W + 8], INK);                /* left edge */
    CHECK_EQ(fb[10 * W + 47], INK);               /* right edge */
    CHECK_EQ(fb[5 * W + 9], PAPER);               /* interior starts one pixel in */
    CHECK_EQ(fb[26 * W + 46], PAPER);
    CHECK_EQ(fb[3 * W + 8], 0xdead);              /* nothing outside */
    CHECK_EQ(fb[28 * W + 8], 0xdead);
    CHECK_EQ(fb[10 * W + 7], 0xdead);
    CHECK_EQ(fb[10 * W + 48], 0xdead);
}

static void test_clip(void) {
    /* Only the rows in the band change; the rest keeps what was there. */
    clear();
    ui_frame_box(fb, W, 10, 16, 0, 8, 32, 24, INK, PAPER);
    for (int x = 0; x < 32; x++) {
        CHECK_EQ(fb[8 * W + x], 0xdead);          /* above the band */
        CHECK_EQ(fb[9 * W + x], 0xdead);
        CHECK_EQ(fb[10 * W + x], x == 0 || x == 31 ? INK : PAPER);
        CHECK_EQ(fb[15 * W + x], x == 0 || x == 31 ? INK : PAPER);
        CHECK_EQ(fb[16 * W + x], 0xdead);         /* band end is exclusive */
        CHECK_EQ(fb[31 * W + x], 0xdead);         /* bottom edge below the band */
    }
}

static void test_full_width(void) {
    /* The option screen is a 128 px box at x=0: both edge columns are drawn. */
    clear();
    ui_frame_box(fb, W, 0, H, 0, 0, W, 40, INK, PAPER);
    CHECK_EQ(fb[0], INK);
    CHECK_EQ(fb[W - 1], INK);
    CHECK_EQ(fb[39 * W], INK);
    CHECK_EQ(fb[W + 1], PAPER);                   /* interior, one row down */
}

int main(void) {
    test_box();
    test_clip();
    test_full_width();
    return test_report("test_ui_frame");
}

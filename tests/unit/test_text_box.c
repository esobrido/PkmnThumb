/* text/box.c — reflow to 15 columns, pagination on <PARA>/<NEXT>, reveal, draw. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen/text.h"
#include "text/box.h"

/* Build a text stream out of ASCII (upper/lower/digits/space and a few marks)
 * so the tests can read like the dialogue they model. */
static int build(uint8_t *out, const char *s) {
    int n = 0;
    out[n++] = PCT_START;
    for (; *s; s++) {
        if (*s == '\n') { out[n++] = PCT_LINE; continue; }
        if (*s == '\f') { out[n++] = PCT_PARA; continue; }
        out[n++] = font_code_from_ascii(*s);
    }
    out[n++] = PCT_DONE;
    return n;
}

static int row_text(const TextBox *b, int row, char *dst) {
    int n = 0;
    for (int c = 0; c < TB_COLS; c++) {
        uint8_t g = textbox_cell(b, row, c);
        if (!g) break;
        if (g == PCT_SPACE) dst[n++] = ' ';
        else if (g >= 0x80 && g <= 0x99) dst[n++] = (char)('A' + g - 0x80);
        else if (g >= 0xa0 && g <= 0xb9) dst[n++] = (char)('a' + g - 0xa0);
        else if (g >= 0xe0 && g <= 0xe9) dst[n++] = (char)('0' + g - 0xe0);
        else if (g == 0x9c) dst[n++] = '.';
        else if (g == 0x9d) dst[n++] = ',';
        else if (g == 0x9e) dst[n++] = '?';
        else if (g == 0x9f) dst[n++] = '!';
        else dst[n++] = '?';
    }
    dst[n] = 0;
    return n;
}

static void test_short_line(void) {
    uint8_t src[128];
    int len = build(src, "Hi there");
    TextBox b;
    textbox_open(&b, src, len, 0);

    assert(textbox_is_open(&b));
    assert(!textbox_waiting(&b));       /* nothing revealed yet */
    assert(!textbox_visible(&b, 0, 0));

    char line[16];
    assert(row_text(&b, 0, line) == 8);
    assert(strcmp(line, "Hi there") == 0);
    assert(textbox_cell(&b, 1, 0) == 0);   /* second row is empty */
}

static void test_reflow_wraps_at_words(void) {
    uint8_t src[256];
    /* 20-column lines from the original; 15 here, so it must re-wrap. */
    int len = build(src, "I hear Prof.Elm\nreally discovered\nsome new Pokemon.");
    TextBox b;
    textbox_open(&b, src, len, 0);

    char l0[16], l1[16], l2[16];
    row_text(&b, 0, l0);
    row_text(&b, 1, l1);
    row_text(&b, 2, l2);

    /* 15 columns exactly, so it stays on one row... */
    assert(strcmp(l0, "I hear Prof.Elm") == 0);
    /* ...but the next original line is 17 and has to be broken. */
    assert(strcmp(l1, "really") == 0);
    assert(strcmp(l2, "discovered some") == 0);
    /* The rest is on the next page, not lost. */
    assert(b.more == 1);

    textbox_update(&b, TB_TICKS * 200);
    textbox_advance(&b);
    row_text(&b, 0, l0);
    assert(strcmp(l0, "new Pokemon.") == 0);
}

static void test_page_break(void) {
    uint8_t src[256];
    int len = build(src, "First page.\fSecond page.");
    TextBox b;
    textbox_open(&b, src, len, 0);

    char line[16];
    row_text(&b, 0, line);
    assert(strcmp(line, "First page.") == 0);

    textbox_update(&b, TB_TICKS * 50);      /* reveal everything */
    assert(textbox_waiting(&b));
    textbox_advance(&b);                     /* page 2 */
    assert(textbox_is_open(&b));
    assert(!textbox_visible(&b, 0, 0));

    textbox_update(&b, TB_TICKS * 50);
    row_text(&b, 0, line);
    assert(strcmp(line, "Second page.") == 0);

    textbox_advance(&b);                     /* last page: closes */
    assert(!textbox_is_open(&b));
}

static void test_reveal_speed(void) {
    uint8_t src[128];
    int len = build(src, "abcdefghijklmno");
    TextBox b;
    textbox_open(&b, src, len, 0);

    textbox_update(&b, 1);
    assert(!textbox_visible(&b, 0, 0));     /* one tick: still nothing */
    textbox_update(&b, 1);
    assert(textbox_visible(&b, 0, 0));      /* two ticks: one glyph */
    assert(!textbox_visible(&b, 0, 1));

    /* The first A press shows the rest of the page without closing it. */
    textbox_advance(&b);
    assert(textbox_visible(&b, 0, 14));
    assert(textbox_is_open(&b));
}

static void test_set_speed(void) {
    uint8_t src[128];
    int len = build(src, "abcdefghijklmno");
    TextBox b;

    /* Instant (the game's INST_TEXT) has the whole page there at once: no
     * frame can be shown with a letter missing. */
    textbox_open(&b, src, len, 0);
    textbox_set_speed(&b, 0);
    textbox_update(&b, 1);
    assert(textbox_visible(&b, 0, 14));
    assert(b.waiting);

    /* Slow is five frames per letter, like PrintLetterDelay's 5/3/1/0 table. */
    textbox_open(&b, src, len, 0);
    textbox_set_speed(&b, 5);
    for (int i = 0; i < 4; i++) textbox_update(&b, 1);
    assert(!textbox_visible(&b, 0, 0));     /* four frames: still nothing */
    textbox_update(&b, 1);
    assert(textbox_visible(&b, 0, 0));      /* the fifth: one letter */
    assert(!textbox_visible(&b, 0, 1));
    /* Frames are honoured in bulk too, not one at a time. */
    textbox_update(&b, 10);
    assert(textbox_visible(&b, 0, 2));
    assert(!textbox_visible(&b, 0, 3));
}

static void test_player_name(void) {
    uint8_t src[128];
    int n = 0;
    /* ASCII letters are NOT charmap bytes: 'Y' is 0x59, which is <PARA>.
     * Everything goes through font_code_from_ascii, as build() does. */
    src[n++] = PCT_START;
    src[n++] = font_code_from_ascii('Y');
    src[n++] = font_code_from_ascii('o');
    src[n++] = PCT_SPACE;
    src[n++] = PCT_PLAYER;
    src[n++] = font_code_from_ascii('!');
    src[n++] = PCT_DONE;

    FontNames names = { "CHRIS", "KRIS" };
    TextBox b;
    textbox_open(&b, src, n, &names);

    char line[16];
    row_text(&b, 0, line);
    assert(strcmp(line, "Yo CHRIS!") == 0);
}

static void test_long_word_and_empty_text(void) {
    uint8_t src[256];
    int len = build(src, "abcdefghijklmnopqrstuvwxyz");
    TextBox b;
    textbox_open(&b, src, len, 0);
    /* Clamped to the width instead of running off the page. */
    char line[16];
    assert(row_text(&b, 0, line) == TB_COLS);

    /* A text with nothing in it must not keep the box alive forever. */
    uint8_t empty[] = { PCT_START, PCT_DONE };
    textbox_open(&b, empty, 2, 0);
    assert(!b.more);
    textbox_update(&b, 10);
    textbox_advance(&b);
    assert(!textbox_is_open(&b));
}

static void test_draw(void) {
    uint8_t src[128];
    int len = build(src, "Hi");
    TextBox b;
    textbox_open(&b, src, len, 0);
    textbox_update(&b, 100);

    enum { W = TB_W, H = TB_H };
    static uint16_t fb[W * H];
    for (int i = 0; i < W * H; i++) fb[i] = 0x1111;
    const uint16_t ink = 0x0000, paper = 0xffff;
    textbox_draw(&b, fb, W, 0, H, 0, 0, ink, paper);

    /* The frame is ink on every edge... */
    assert(fb[0] == ink);
    assert(fb[W - 1] == ink);
    assert(fb[(H - 1) * W] == ink);
    /* ...the inside is paper where there is no glyph... */
    assert(fb[H * W - 2] == ink);          /* bottom-right corner */
    assert(fb[5 * W + 60] == paper);       /* right of "Hi" */
    /* ...and at least one glyph pixel got inked in the first cell. */
    int lit = 0;
    for (int y = 4; y < 12; y++)
        for (int x = 4; x < 12; x++)
            if (fb[y * W + x] == ink) lit++;
    assert(lit > 4);

    /* Once the page is shown, a ▼ blinks in the bottom-right cell. */
    static uint16_t fb3[W * H];
    for (int i = 0; i < W * H; i++) fb3[i] = paper;
    textbox_update(&b, TB_TICKS * 10);       /* waiting; (ticks/16) odd -> arrow on */
    assert(textbox_waiting(&b));
    textbox_draw(&b, fb3, W, 0, H, 0, 0, ink, paper);
    int arrow = 0;
    int ax = 4 + (TB_COLS - 1) * 8, ay = 4 + (TB_ROWS - 1) * 8;
    for (int y = ay; y < ay + 8; y++)
        for (int x = ax; x < ax + 8; x++)
            if (fb3[y * W + x] == ink) arrow++;
    assert(arrow > 3);

    /* Clipping: a band that does not touch the box must not write anything. */
    static uint16_t fb2[W * H];
    for (int i = 0; i < W * H; i++) fb2[i] = 0x1111;
    textbox_draw(&b, fb2, W, 64, 128, 0, 0, ink, paper);
    assert(fb2[0] == 0x1111);
}

int main(void) {
    test_short_line();
    test_reflow_wraps_at_words();
    test_page_break();
    test_reveal_speed();
    test_set_speed();
    test_player_name();
    test_long_word_and_empty_text();
    test_draw();
    printf("text/box: ok\n");
    return 0;
}

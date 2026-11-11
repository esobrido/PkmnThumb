/* text/font.c — glyph lookup and multi-glyph expansion. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gen/font.h"
#include "gen/text.h"
#include "text/font.h"

#define W 128
#define H 128

static void test_plain_glyphs(void) {
    /* 'A' is the first tile of the sheet the game loads at $80. */
    const uint8_t *a = font_glyph(0x80);
    assert(a != 0);
    /* It has ink somewhere, and the same shape for the same code. */
    int ink = 0;
    for (int i = 0; i < 8; i++) ink += a[i] != 0;
    assert(ink > 2);
    assert(font_glyph(0x80) == a);

    /* Space is printable but has no art: the textbox draws paper. */
    assert(font_is_glyph(PCT_SPACE));
    assert(font_glyph(PCT_SPACE) == 0);

    /* 'a' and '0' exist too, at the charmap offsets. */
    assert(font_glyph(0xa0) != 0);
    assert(font_glyph(0xe0) != 0);

    /* Control bytes are not glyphs. */
    assert(!font_is_glyph(PCT_START));
    assert(!font_is_glyph(PCT_PARA));
    assert(!font_is_glyph(PCT_DONE));

    /* common.png tiles at $f2 (arrows, prime marks, phone, black). */
    assert(font_is_glyph(PC_FONT_COMMON_FIRST));
    assert(font_glyph(PC_FONT_COMMON_FIRST) != 0);
    /* Out of the font range (the frame tiles) -> no art. */
    assert(font_glyph(0xf8) == 0);
}

static void test_expand(void) {
    uint8_t out[FONT_EXPAND_MAX];
    FontNames names = { "CHRIS", "KRIS" };

    /* The n-grams print ordinary letters, not a special glyph. */
    int n = font_expand(PCT_POKE, &names, out);
    assert(n == 4);
    assert(out[0] == 0x8f && out[1] == 0xae && out[2] == 0xaa && out[3] == 0xc8);

    n = font_expand(PCT_POKEMON, &names, out);
    assert(n == 7);
    assert(out[4] == 0xac && out[6] == 0xad);

    /* Names go through the ASCII -> charmap mapping. */
    n = font_expand(PCT_PLAYER, &names, out);
    assert(n == 5);
    assert(out[0] == 'C' - 'A' + 0x80);
    assert(out[4] == 'S' - 'A' + 0x80);

    n = font_expand(PCT_RIVAL, &names, out);
    assert(n == 4 && out[0] == 'K' - 'A' + 0x80);

    /* No names: something must print rather than nothing. */
    n = font_expand(PCT_PLAYER, 0, out);
    assert(n == 3 && out[0] == 0x9e);

    /* A name longer than a GB name is capped so it always fits the buffer. */
    FontNames longname = { "ABCDEFGHIJK", "X" };
    n = font_expand(PCT_PLAYER, &longname, out);
    assert(n == 7);

    /* Plain glyphs expand to nothing: the caller keeps the code. */
    assert(font_expand(0x80, &names, out) == 0);
    assert(font_expand(PCT_SPACE, &names, out) == 0);
}

static void test_ascii_codes(void) {
    assert(font_code_from_ascii('A') == 0x80);
    assert(font_code_from_ascii('Z') == 0x99);
    assert(font_code_from_ascii('a') == 0xa0);
    assert(font_code_from_ascii('z') == 0xb9);
    assert(font_code_from_ascii('5') == 0xe5);
    assert(font_code_from_ascii('.') == 0x9c);
    assert(font_code_from_ascii('/') == 0xdc);
    assert(font_code_from_ascii('~') == 0x9e);   /* unknown -> '?' */
}

static void test_drawing(void) {
    static uint16_t fb[W * H];
    for (int i = 0; i < W * H; i++) fb[i] = 0x1111;

    /* 'A' has ink somewhere in its 8x8 cell, and only inside it. */
    font_draw_glyph(fb, W, 0, H, 10, 10, font_code_from_ascii('A'), 0x2222);
    int ink = 0, outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int set = fb[y * W + x] == 0x2222;
            if (!set) continue;
            ink++;
            if (x < 10 || x > 17 || y < 10 || y > 17) outside++;
        }
    assert(ink > 0 && outside == 0);

    /* A blank cell (space) writes nothing: the paper stays where it was. */
    for (int i = 0; i < W * H; i++) fb[i] = 0x1111;
    font_draw_glyph(fb, W, 0, H, 10, 10, PCT_SPACE, 0x2222);
    for (int i = 0; i < W * H; i++) assert(fb[i] == 0x1111);

    /* A text stream draws one cell per glyph and reports the width used. */
    uint8_t stream[4];
    stream[0] = font_code_from_ascii('H');
    stream[1] = PCT_POKEMON;
    stream[2] = PCT_SPACE;
    stream[3] = font_code_from_ascii('!');
    for (int i = 0; i < W * H; i++) fb[i] = 0x1111;
    int w = font_draw_text(stream, 4, 0, fb, W, 0, H, 0, 0, W, 0x2222);
    /* H + P o k é m o n + space + ! = 10 cells. */
    assert(w == 80);
    /* maxw clips: the space and '!' do not fit in 8 cells. */
    int w2 = font_draw_text(stream, 4, 0, fb, W, 0, H, 0, 0, 64, 0x2222);
    assert(w2 == 64);

    /* A band that misses the glyph leaves the buffer alone. */
    for (int i = 0; i < W * H; i++) fb[i] = 0x1111;
    font_draw_glyph(fb, W, 64, 128, 10, 10, font_code_from_ascii('A'), 0x2222);
    for (int i = 0; i < W * H; i++) assert(fb[i] == 0x1111);
}

int main(void) {
    test_plain_glyphs();
    test_expand();
    test_ascii_codes();
    test_drawing();
    printf("text/font: ok\n");
    return 0;
}

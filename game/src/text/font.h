/*
 * text/font.h — the game's 8x8 font, as 1bpp rows.
 *
 * `gen/font.h` holds the 114 tiles the game loads at $80 (so 'A' is tile 0) and
 * the 6 "common" tiles at $f2. A text byte is not a character: it is a slot in
 * that table, and a few slots are not one glyph at all — `#` prints "Poké" from
 * four ordinary glyphs, `<PLAYER>` prints the player's name, and so on. That
 * expansion belongs here so the textbox only ever deals with glyph codes.
 *
 * Pure logic over plain arrays: no Mote calls, no globals.
 */
#ifndef TEXT_FONT_H
#define TEXT_FONT_H

#include <stdint.h>

/* Longest expansion ("Pokémon" = 7 glyphs, a name is capped at 7). */
#define FONT_EXPAND_MAX 8

/* Names the game substitutes into text; both are capped at 7 glyphs (a GB name
 * is 7 characters) so an expansion always fits FONT_EXPAND_MAX. */
typedef struct {
    const char *player;   /* <PLAYER> */
    const char *rival;    /* <RIVAL>  */
} FontNames;

/* Printable glyph code in the game's charmap, or 0 when the code is a control
 * byte the textbox must interpret (see PCT_* in gen/text.h). 0x7f (space) is
 * printable but has no art: it is a blank cell. */
int font_is_glyph(uint8_t code);

/* 8 rows, 1bpp, bit 7 = leftmost pixel, 1 = ink. NULL for codes with no art
 * (space, and anything outside the font). */
const uint8_t *font_glyph(uint8_t code);

/* Charcode ('A', 'a', '0', ...) -> glyph code, or 0x9e ('?') if unsupported. */
uint8_t font_code_from_ascii(char c);

/* Expand a code that stands for several glyphs:
 *   PCT_POKE    -> P o k é          PCT_POKEMON -> P o k é m o n
 *   PCT_PLAYER  -> the player's name   PCT_RIVAL -> the rival's name
 *   PCT_TRENDY  -> "???"
 * Returns how many glyph codes were written to `out` (0 for a plain glyph, so
 * the caller just uses the code as it is). `names` may be NULL for defaults. */
int font_expand(uint8_t code, const FontNames *names, uint8_t *out);

/* ---- drawing ------------------------------------------------------------
 * Pixels go into a framebuffer the caller owns, clipped to the band [y0, y1)
 * because the port rasterises the panel in horizontal strips on both cores.
 * `ink` is the colour of a set bit; a blank cell is left untouched, so the caller
 * decides what "paper" is.
 */

void font_draw_glyph(uint16_t *fb, int fbw, int y0, int y1, int x, int y,
                     uint8_t code, uint16_t ink);

/* A whole text stream: n-grams and <PLAYER> are expanded here, so the caller
 * only deals with bytes. Prints at most `maxw` pixels and returns the width it
 * used, so a caller can lay out a right-hand column. */
int font_draw_text(const uint8_t *bytes, int len, const FontNames *names,
                   uint16_t *fb, int fbw, int y0, int y1, int x, int y,
                   int maxw, uint16_t ink);

#endif

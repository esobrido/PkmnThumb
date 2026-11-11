#include "text/font.h"

#include "gen/font.h"
#include "gen/text.h"

/* "Poké" and "Pokémon" as the four/seven ordinary glyphs, so the n-gram slots
 * print exactly what the original prints. */
static const uint8_t k_poke[]    = { 0x8f, 0xae, 0xaa, 0xc8 };            /* P o k é */
static const uint8_t k_pokemon[] = { 0x8f, 0xae, 0xaa, 0xc8, 0xac, 0xae, 0xad };
static const uint8_t k_unknown[] = { 0x9e };                              /* ? */

static int copy(const uint8_t *src, int n, uint8_t *out) {
    if (n > FONT_EXPAND_MAX) n = FONT_EXPAND_MAX;
    for (int i = 0; i < n; i++) out[i] = src[i];
    return n;
}

int font_is_glyph(uint8_t code) {
    if (code == PCT_SPACE) return 1;
    if (code >= PC_FONT_FIRST && code < PC_FONT_FIRST + PC_FONT_TILES) return 1;
    if (code >= PC_FONT_COMMON_FIRST &&
        code < PC_FONT_COMMON_FIRST + PC_FONT_COMMON_TILES) return 1;
    return 0;
}

const uint8_t *font_glyph(uint8_t code) {
    if (code >= PC_FONT_FIRST && code < PC_FONT_FIRST + PC_FONT_TILES)
        return pc_font[code - PC_FONT_FIRST];
    if (code >= PC_FONT_COMMON_FIRST &&
        code < PC_FONT_COMMON_FIRST + PC_FONT_COMMON_TILES)
        return pc_font_common[code - PC_FONT_COMMON_FIRST];
    return 0;   /* space and anything else: the cell keeps the paper colour */
}

uint8_t font_code_from_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x80 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0xa0 + (c - 'a'));
    if (c >= '0' && c <= '9') return (uint8_t)(0xe0 + (c - '0'));
    if (c == ' ') return PCT_SPACE;
    if (c == '.') return 0x9c;
    if (c == ',') return 0x9d;
    if (c == '?') return 0x9e;
    if (c == '!') return 0x9f;
    if (c == '-') return 0xbc;
    if (c == ':') return 0xbd;
    if (c == '/') return 0xdc;      /* "19/19" in the battle HP line */
    if (c == '\'') return 0xc0;
    return 0x9e;
}

int font_expand(uint8_t code, const FontNames *names, uint8_t *out) {
    switch (code) {
    case PCT_POKE:    return copy(k_poke, (int)sizeof k_poke, out);
    case PCT_POKEMON: return copy(k_pokemon, (int)sizeof k_pokemon, out);
    case PCT_TRENDY:  return copy(k_unknown, 1, out);
    case PCT_PLAYER:
    case PCT_RIVAL: {
        const char *s = 0;
        if (names) s = (code == PCT_PLAYER) ? names->player : names->rival;
        if (!s || !*s) s = "???";
        int n = 0;
        for (; s[n] && n < 7; n++) out[n] = font_code_from_ascii(s[n]);
        return n;
    }
    default:
        return 0;
    }
}

/* ---- drawing ------------------------------------------------------------ */

static void put_px(uint16_t *fb, int fbw, int y0, int y1, int x, int y, uint16_t c) {
    if (x < 0 || x >= fbw || y < y0 || y >= y1) return;
    fb[y * fbw + x] = c;
}

void font_draw_glyph(uint16_t *fb, int fbw, int y0, int y1, int x, int y,
                     uint8_t code, uint16_t ink) {
    const uint8_t *g = font_glyph(code);
    if (!g) return;
    for (int sy = 0; sy < 8; sy++) {
        uint8_t bits = g[sy];
        for (int sx = 0; sx < 8; sx++)
            if (bits & (1u << (7 - sx))) put_px(fb, fbw, y0, y1, x + sx, y + sy, ink);
    }
}

int font_draw_text(const uint8_t *bytes, int len, const FontNames *names,
                   uint16_t *fb, int fbw, int y0, int y1, int x, int y,
                   int maxw, uint16_t ink) {
    int px = 0;
    for (int i = 0; i < len; i++) {
        uint8_t sub[FONT_EXPAND_MAX];
        int n = font_expand(bytes[i], names, sub);
        if (!n) {
            sub[0] = bytes[i];
            n = 1;
        }
        for (int k = 0; k < n; k++) {
            if (px + 8 > maxw) return px;
            font_draw_glyph(fb, fbw, y0, y1, x + px, y, sub[k], ink);
            px += 8;
        }
    }
    return px;
}

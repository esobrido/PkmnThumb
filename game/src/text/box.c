#include "text/box.h"

#include "gen/text.h"

/* What next_glyph() returns besides a glyph code. */
enum { G_EOL = 0x100, G_EOP, G_END };
/* Why read_word() stopped. */
enum { BRK_NONE = 0, BRK_EOL, BRK_EOP, BRK_END };

static int next_glyph(TextBox *b) {
    if (b->sub_at < b->sub_len) return b->sub[b->sub_at++];

    for (;;) {
        if (b->pos >= b->len) return G_END;
        uint8_t code = b->src[b->pos++];

        switch (code) {
        case PCT_START:
            continue;                     /* <START>: nothing to print */
        case PCT_DONE:
        case PCT_PROMPT:
        case 0x53:                        /* '@' terminator */
            return G_END;
        case PCT_LNBRK:
        case PCT_LINE:
        case PCT_CONT:
            return G_EOL;
        case PCT_NEXT:
        case PCT_PARA:
            return G_EOP;
        default:
            break;
        }

        int n = font_expand(code, &b->names, b->sub);
        if (n > 0) {
            b->sub_len = n;
            b->sub_at = 1;
            return b->sub[0];
        }
        b->sub_len = b->sub_at = 0;
        return code;                      /* a plain glyph, or space */
    }
}

/* Read one word: stops *before* consuming a line/page/end break, which it
 * reports through brk. A run of spaces yields n = 0 with BRK_NONE. */
static int read_word(TextBox *b, int *brk) {
    int n = 0;
    for (;;) {
        int g = next_glyph(b);
        if (g == G_EOL) { *brk = BRK_EOL; return n; }
        if (g == G_EOP) { *brk = BRK_EOP; return n; }
        if (g == G_END) { *brk = BRK_END; return n; }
        if (g == PCT_SPACE) { *brk = BRK_NONE; return n; }
        if (n < TB_COLS) b->word[n] = (uint8_t)g;
        n++;
        if (n > TB_COLS) n = TB_COLS;     /* overlong word: clamp, rest is cut */
    }
}

static void page_clear(TextBox *b) {
    for (int r = 0; r < TB_ROWS; r++)
        for (int c = 0; c < TB_COLS; c++) { b->cell[r][c] = 0; b->seq[r][c] = 0; }
    b->seq_count = 0;
    b->revealed = 0;
    b->wait_frames = 0;
    b->waiting = 0;
    b->more = 0;
}

/* Write one word, wrapping and paginating. A word that does not fit in the
 * current page is stored in `pend` for the next one. */
static void put_word(TextBox *b, int *row, int *col, const uint8_t *w, int n) {
    if (*col > 0) {
        /* The separator is not part of any word, so it is re-inserted here: one
         * space between words, collapsed. If it no longer fits, wrap first. */
        if (*col + 1 + n > TB_COLS) {
            *col = 0;
            (*row)++;
        } else {
            if (*row < TB_ROWS) {
                b->cell[*row][*col] = PCT_SPACE;
                b->seq[*row][*col] = (uint8_t)(++b->seq_count);
            }
            (*col)++;
        }
    }
    if (*col > 0 && *col + n > TB_COLS) { *col = 0; (*row)++; }
    if (*row >= TB_ROWS) {
        for (int i = 0; i < n && i < TB_COLS; i++) b->pend[i] = w[i];
        b->pend_len = n;
        b->more = 1;
        *row = TB_ROWS;
        return;
    }
    for (int i = 0; i < n; i++, (*col)++) {
        b->cell[*row][*col] = w[i];
        b->seq[*row][*col] = (uint8_t)(++b->seq_count);
    }
}

static void layout(TextBox *b) {
    page_clear(b);

    int row = 0, col = 0;
    if (b->pend_len) {
        uint8_t w[TB_COLS];
        int n = b->pend_len;
        for (int i = 0; i < n; i++) w[i] = b->pend[i];
        b->pend_len = 0;
        put_word(b, &row, &col, w, n);
    }

    while (row < TB_ROWS) {
        int brk = BRK_NONE;
        int n = read_word(b, &brk);
        if (n > 0) put_word(b, &row, &col, b->word, n);
        if (row >= TB_ROWS) { b->more = 1; break; }

        if (brk == BRK_END) break;
        if (brk == BRK_EOP) { b->more = 1; break; }
        /* BRK_EOL is deliberately ignored: the original broke its lines at 20
         * columns and this panel fits 15, so keeping those breaks would leave
         * ragged half-lines. The reflow is what decides where a line ends. */
    }

    /* A page that holds nothing because the whole text is one empty line still
     * has to end the box, not loop forever. */
    if (b->seq_count == 0 && b->more) b->more = 0;
}

void textbox_open(TextBox *b, const uint8_t *src, int len, const FontNames *names) {
    b->src = src;
    b->len = len;
    b->pos = 0;
    b->sub_len = b->sub_at = 0;
    b->pend_len = 0;
    b->ticks = 0;            /* the ▼ blink starts with the box */
    b->ticks_per_glyph = TB_TICKS;
    b->open = 1;
    b->names.player = names ? names->player : 0;
    b->names.rival = names ? names->rival : 0;
    layout(b);
}

void textbox_update(TextBox *b, int frames) {
    if (!b->open) return;
    b->ticks += frames;              /* keeps blinking while waiting for A */
    if (b->waiting) return;
    if (b->ticks_per_glyph <= 0) {   /* Instant: the page is there at once */
        b->revealed = b->seq_count;
        b->wait_frames = 0;
    } else {
        b->wait_frames += frames;
        while (b->wait_frames >= b->ticks_per_glyph && b->revealed < b->seq_count) {
            b->wait_frames -= b->ticks_per_glyph;
            b->revealed++;
        }
    }
    if (b->revealed >= b->seq_count) {
        b->revealed = b->seq_count;
        b->waiting = 1;
        b->wait_frames = 0;
    }
}

void textbox_set_speed(TextBox *b, int ticks) {
    if (ticks < 0) ticks = 0;
    b->ticks_per_glyph = ticks;
}

void textbox_advance(TextBox *b) {
    if (!b->open) return;
    if (!b->waiting) {           /* first press: show the whole page at once */
        b->revealed = b->seq_count;
        b->waiting = 1;
        return;
    }
    if (b->more) layout(b);
    else b->open = 0;
}

int textbox_is_open(const TextBox *b) { return b->open; }
int textbox_waiting(const TextBox *b) { return b->waiting; }

uint8_t textbox_cell(const TextBox *b, int row, int col) {
    if (row < 0 || row >= TB_ROWS || col < 0 || col >= TB_COLS) return 0;
    return b->cell[row][col];
}

int textbox_visible(const TextBox *b, int row, int col) {
    if (row < 0 || row >= TB_ROWS || col < 0 || col >= TB_COLS) return 0;
    return b->seq[row][col] != 0 && (int)b->seq[row][col] <= b->revealed;
}

void textbox_draw(const TextBox *b, uint16_t *fb, int fbw, int y0, int y1,
                  int x, int y, uint16_t ink, uint16_t paper) {
    int x0 = x < 0 ? 0 : x;
    int x1 = x + TB_W;
    if (x1 > fbw) x1 = fbw;

    for (int sy = y; sy < y + TB_H; sy++) {
        if (sy < y0 || sy >= y1 || sy < 0) continue;
        uint16_t *row = &fb[sy * fbw];
        int edge = (sy == y || sy == y + TB_H - 1);
        for (int sx = x0; sx < x1; sx++) {
            int border = edge || sx == x || sx == x + TB_W - 1;
            row[sx] = border ? ink : paper;
        }
    }

    for (int r = 0; r < TB_ROWS; r++) {
        int gy = y + 4 + r * 8;
        for (int c = 0; c < TB_COLS; c++) {
            if (!textbox_visible(b, r, c)) continue;
            const uint8_t *glyph = font_glyph(b->cell[r][c]);
            if (!glyph) continue;             /* space: paper, nothing to draw */
            int gx = x + 4 + c * 8;
            for (int sy = 0; sy < 8; sy++) {
                int py = gy + sy;
                if (py < y0 || py >= y1) continue;
                uint16_t *dst = &fb[py * fbw];
                uint8_t bits = glyph[sy];
                for (int sx = 0; sx < 8; sx++) {
                    if (gx + sx >= 0 && gx + sx < fbw && (bits & (1u << (7 - sx))))
                        dst[gx + sx] = ink;
                }
            }
        }
    }

    /* The game blinks a ▼ in the bottom-right corner once the page is fully
     * shown: the player knows there is another page (or that the box ends). */
    if (b->waiting && ((b->ticks / 16) & 1)) {
        const uint8_t *glyph = font_glyph(TB_ARROW);
        int gx = x + 4 + (TB_COLS - 1) * 8;
        int gy = y + 4 + (TB_ROWS - 1) * 8;
        if (glyph)
            for (int sy = 0; sy < 8; sy++) {
                int py = gy + sy;
                if (py < y0 || py >= y1) continue;
                uint16_t *dst = &fb[py * fbw];
                for (int sx = 0; sx < 8; sx++) {
                    if (gx + sx >= 0 && gx + sx < fbw && (glyph[sy] & (1u << (7 - sx))))
                        dst[gx + sx] = ink;
                }
            }
    }
}

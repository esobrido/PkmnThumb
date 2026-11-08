#include "text/box.h"

#include "gen/text.h"

/* What next_glyph() returns besides a glyph code. G_SPACE is a space *in the
 * stream*, which separates words; a space that an expansion produced (the
 * padding of a <NUM>, say) is an ordinary glyph and must not break the word. */
enum { G_EOL = 0x100, G_EOP, G_END, G_SPACE };
/* Why read_word() stopped. */
enum { BRK_NONE = 0, BRK_EOL, BRK_EOP, BRK_END };

/* Skip n argument bytes, without running past the end of the stream. */
static void skip_args(TextBox *b, int n) {
    b->pos += n;
    if (b->pos > b->len) b->pos = b->len;
}

/* <NUM>: address low, address high, format (low nibble = digits).
 * Fills b->sub with the digits and returns how many. */
static int read_number(TextBox *b) {
    uint16_t addr = 0;
    int fmt = 1;
    if (b->pos + 1 < b->len) addr = (uint16_t)(b->src[b->pos] | (b->src[b->pos + 1] << 8));
    if (b->pos + 2 < b->len) fmt = b->src[b->pos + 2];
    skip_args(b, 3);

    int digits = fmt & 0x0F;
    if (digits < 1) digits = 1;
    if (digits > TB_RUN_MAX) digits = TB_RUN_MAX;

    int known = 0, value = 0;
    for (int i = 0; i < b->vars.num_count; i++)
        if (b->vars.nums[i].addr == addr) { value = b->vars.nums[i].value; known = 1; break; }
    if (!known) {                       /* no value for that address: say so */
        b->sub[0] = font_code_from_ascii('?');
        return 1;
    }

    int neg = value < 0;
    unsigned int v = neg ? (unsigned int)(-(value + 1)) + 1u : (unsigned int)value;
    uint8_t rev[TB_RUN_MAX];
    int rn = 0;
    do { rev[rn++] = font_code_from_ascii((char)('0' + v % 10)); v /= 10; }
    while (v && rn < TB_RUN_MAX);

    int total = rn + (neg ? 1 : 0);
    int pad = digits > total ? digits - total : 0;
    int n = 0;
    for (int i = 0; i < pad && n < TB_RUN_MAX; i++) b->sub[n++] = PCT_SPACE;
    if (neg && n < TB_RUN_MAX) b->sub[n++] = font_code_from_ascii('-');
    while (rn > 0 && n < TB_RUN_MAX) b->sub[n++] = rev[--rn];
    return n;
}

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

        /* ---- stalls: they delay the next glyph, they are not glyphs ---- */
        case PCT_WAIT:                    /* wait for A, arrow blinking */
            b->stop_pending = TB_STOP_KEY;
            continue;
        case PCT_PAUSE:                   /* 30 frames, or A/B */
            b->stop_pending = TB_STOP_DELAY;
            continue;

        /* ---- commands with arguments: consumed, then skipped ---- */
        case PCT_NUM: {
            int n = read_number(b);
            if (n > 0) {
                b->sub_len = n;
                b->sub_at = 1;
                return b->sub[0];
            }
            continue;
        }
        case PCT_SOUND:                   /* sfx id: no audio yet */
            skip_args(b, 1);
            continue;
        case PCT_RAM:                     /* string in WRAM */
            skip_args(b, 2);
            continue;
        case PCT_FAR:                     /* bank + string in another bank */
            skip_args(b, 3);
            continue;

        case PCT_ASM:
            /* The original jumps into the bytes that follow (inline code) or,
             * from WRAM, overwrites the command with '@'. There is no code to
             * run here, and printing those bytes as glyphs would be nonsense,
             * so the stream ends: the safe half of the original's behaviour. */
            return G_END;

        /* ---- nothing to print and no argument ---- */
        case PCT_DAY:                     /* needs the clock (SYS-05) */
        case PCT_TARGET:
        case PCT_USER:
        case PCT_ENEMY:
        case PCT_CTXT:                    /* compression marker */
        case PCT_PLURAL:                  /* decided at build time */
            continue;
        default:
            break;
        }

        if (code == PCT_SPACE) return G_SPACE;

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
 * reports through brk. A run of spaces yields n = 0 with BRK_NONE.
 *
 * A <WAIT>/<PAUSE> inside the word is remembered as `word_stop_at`, the index of
 * the glyph it has to stall in front of: the stall happens at that exact place in
 * the stream, which can be mid-word ("are you<WAIT>ready?"). */
static int read_word(TextBox *b, int *brk) {
    int n = 0;
    b->word_stop_at = -1;
    b->word_stop_kind = TB_STOP_NONE;
    for (;;) {
        int g = next_glyph(b);
        if (g == G_EOL) { *brk = BRK_EOL; return n; }
        if (g == G_EOP) { *brk = BRK_EOP; return n; }
        if (g == G_END) { *brk = BRK_END; return n; }
        if (g == G_SPACE) { *brk = BRK_NONE; return n; }
        if (b->stop_pending != TB_STOP_NONE && n < TB_COLS && b->word_stop_at < 0) {
            b->word_stop_at = n;
            b->word_stop_kind = b->stop_pending;
            b->stop_pending = TB_STOP_NONE;
        }
        if (n < TB_COLS) b->word[n] = (uint8_t)g;
        n++;
        if (n > TB_COLS) n = TB_COLS;     /* overlong word: clamp, rest is cut */
    }
}

static void page_clear(TextBox *b) {
    for (int r = 0; r < TB_ROWS; r++)
        for (int c = 0; c < TB_COLS; c++) { b->cell[r][c] = 0; b->seq[r][c] = 0; }
    for (int i = 0; i <= TB_ROWS * TB_COLS; i++) b->stop[i] = TB_STOP_NONE;
    b->seq_count = 0;
    b->revealed = 0;
    b->wait_frames = 0;
    b->waiting = 0;
    b->more = 0;
    b->hold = 0;
    /* A <WAIT>/<PAUSE> left over at the very end of the previous page is
     * dropped: the box already waits for A there, which is longer. */
    b->stop_pending = TB_STOP_NONE;
}

/* Put one glyph in a cell, remembering whether the printer must stall before
 * revealing it. */
static void place(TextBox *b, int row, int col, uint8_t code, int stop) {
    b->cell[row][col] = code;
    b->seq[row][col] = (uint8_t)(++b->seq_count);
    if (stop != TB_STOP_NONE && b->seq_count <= TB_ROWS * TB_COLS)
        b->stop[b->seq_count] = (uint8_t)stop;
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
                int stop = b->stop_pending;
                b->stop_pending = TB_STOP_NONE;   /* the stall waits for the space */
                place(b, *row, *col, PCT_SPACE, stop);
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
        int stop = b->stop_pending;
        b->stop_pending = TB_STOP_NONE;
        if (i == b->word_stop_at) stop = b->word_stop_kind;
        place(b, *row, *col, w[i], stop);
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

void textbox_open(TextBox *b, const uint8_t *src, int len, const TextVars *vars) {
    b->src = src;
    b->len = len;
    b->pos = 0;
    b->sub_len = b->sub_at = 0;
    b->pend_len = 0;
    b->stop_pending = TB_STOP_NONE;
    b->word_stop_at = -1;
    b->word_stop_kind = TB_STOP_NONE;
    b->hold = 0;
    b->ticks = 0;            /* the ▼ blink starts with the box */
    b->ticks_per_glyph = TB_TICKS;
    b->open = 1;
    b->names.player = 0;
    b->names.rival = 0;
    b->vars.nums = 0;
    b->vars.num_count = 0;
    if (vars) {
        b->names = vars->names;
        b->vars = *vars;
    }
    layout(b);
}

/* Show everything up to the next <WAIT>, or to the end of the page. */
static void reveal_all(TextBox *b) {
    for (int k = b->revealed + 1; k <= b->seq_count && k <= TB_ROWS * TB_COLS; k++)
        if (b->stop[k] == TB_STOP_KEY) {
            b->revealed = k - 1;
            b->waiting = 1;
            b->wait_frames = 0;
            return;
        }
    b->revealed = b->seq_count;
    b->waiting = 1;
    b->wait_frames = 0;
}

void textbox_update(TextBox *b, int frames) {
    if (!b->open) return;
    b->ticks += frames;              /* keeps blinking while waiting for A */
    if (b->waiting) return;

    if (b->hold > 0) {               /* <PAUSE>: a stall nobody has to press */
        b->hold -= frames;
        if (b->hold > 0) return;
        b->hold = 0;
    }

    if (b->ticks_per_glyph <= 0) {   /* Instant: the page is there at once */
        reveal_all(b);
        return;
    }

    b->wait_frames += frames;
    for (;;) {
        if (b->revealed >= b->seq_count) {
            b->waiting = 1;
            b->wait_frames = 0;
            return;
        }
        int next = b->revealed + 1;
        int stop = next <= TB_ROWS * TB_COLS ? b->stop[next] : TB_STOP_NONE;
        if (stop == TB_STOP_KEY) {   /* <WAIT>: the arrow blinks, A continues */
            b->waiting = 1;
            b->wait_frames = 0;
            return;
        }
        if (stop == TB_STOP_DELAY) { /* <PAUSE>: keep printing after the stall */
            b->stop[next] = TB_STOP_NONE;   /* passed: it must not stall twice */
            b->hold = TB_PAUSE_FRAMES;
            b->wait_frames = 0;
            return;
        }
        if (b->wait_frames < b->ticks_per_glyph) return;
        b->wait_frames -= b->ticks_per_glyph;
        b->revealed++;
    }
}

void textbox_set_speed(TextBox *b, int ticks) {
    if (ticks < 0) ticks = 0;
    b->ticks_per_glyph = ticks;
}

void textbox_advance(TextBox *b) {
    if (!b->open) return;
    if (!b->waiting) {           /* first press: show the whole page at once */
        reveal_all(b);
        return;
    }
    if (b->revealed < b->seq_count) {   /* a <WAIT> mid-page: carry on printing */
        int next = b->revealed + 1;
        if (next <= TB_ROWS * TB_COLS) b->stop[next] = TB_STOP_NONE;
        b->waiting = 0;
        b->wait_frames = 0;
        b->hold = 0;
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

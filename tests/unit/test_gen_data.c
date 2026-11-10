/* gen data — the extracted tables must be internally consistent.
 *
 * The extractor validates the raw data (sizes, id ranges) while it writes it; this
 * test validates what actually ended up in the headers the game compiles: every
 * index a PcObject holds must point at something, and every text stream must be
 * a complete one (<START> ... <DONE>). A silently shifted index (the classic
 * "one NPC speaks another NPC's line") fails here instead of on the handheld.
 */
#include <assert.h>
#include <stdio.h>

#include "gen/font.h"
#include "gen/options.h"
#include "gen/text.h"
#include "text/font.h"
#include "gen/world.h"
#include "ow/clock.h"

int main(void) {
    /* ---- texts ---- */
    assert(PC_TEXT_COUNT > 0);
    for (int i = 0; i < PC_TEXT_COUNT; i++) {
        const PcText *t = &pc_texts[i];
        assert(t->label && *t->label);
        assert(t->len >= 2);
        assert(t->bytes[0] == PCT_START);
        /* The stream either ends the box or waits for A. */
        uint8_t last = t->bytes[t->len - 1];
        assert(last == PCT_DONE || last == PCT_PROMPT);
        /* A text with no printable glyphs is a broken extraction. */
        int glyphs = 0;
        for (int j = 1; j < t->len - 1; j++) {
            uint8_t c = t->bytes[j];
            if (c == PCT_SPACE || c >= 0x80) glyphs++;
        }
        assert(glyphs > 0);
    }

    /* ---- the clock's palettes ---- */
    assert((int)BG_PAL_GROUPS == (int)OW_TOD_COUNT);
    for (int g = 0; g < BG_PAL_GROUPS; g++) {
        /* The four groups have to differ: an extractor that read the same rows
         * four times would compile and never change the picture. */
        int same = 1;
        for (int i = 0; i < BG_PAL_COUNT && same; i++)
            for (int c = 0; c < 4; c++)
                if (bg_pal[g][i][c] != bg_pal[OW_TOD_DAY][i][c]) same = 0;
        assert(g == OW_TOD_DAY || !same);
        /* Slot 7 is the text palette: the same white-on-black in every group, so
         * the menus do not change colour with the hour. */
        for (int c = 0; c < 4; c++)
            assert(bg_pal[g][7][c] == bg_pal[OW_TOD_DAY][7][c]);
    }

    /* ---- maps and their objects ---- */
    assert(PC_MAP_COUNT > 0);
    int timed_npcs = 0;
    for (int m = 0; m < PC_MAP_COUNT; m++) {
        const PcMapDef *map = &pc_maps[m];
        assert(map->name && *map->name);
        assert(map->w > 0 && map->h > 0);
        assert(map->blocks);
        assert(map->tileset < PC_TS_COUNT);
        /* The header's time field is one of the six PALETTE_* constants, and the
         * environment flag says whether the map uses the interior palette set. */
        assert(map->pal <= OW_PAL_INDOOR);
        assert(map->indoor <= 1);
        for (int i = 0; i < map->object_count; i++) {
            const PcObject *o = &map->objects[i];
            /* Objects are on the 16 px grid inside the map. */
            assert(o->x < map->w * 32 && o->y < map->h * 32);
            assert(o->sprite < PC_SPRITE_COUNT);
            assert(o->dir <= 3 && o->anim <= 3);
            /* -1 (no dialogue) or a real text. This is the index that would
             * silently break if map_objects() and map_object_texts() ever
             * disagreed about which objects exist. */
            assert(o->text >= -1 && o->text < PC_TEXT_COUNT);
            /* The time-of-day byte is 0xff (always out) or a mask of the four
             * phases: no other bits, and never a mask that hides it all day. */
            assert(o->time_mask == 0xff ||
                   ((o->time_mask & 0x0f) != 0 && (o->time_mask & 0xf0) == 0));
            if (o->time_mask != 0xff) timed_npcs++;
        }
    }

    /* Some NPCs are only out at certain hours (New Bark's schoolboy is
     * MORN|DAY), which is what makes the clock visible in the overworld. */
    assert(timed_npcs > 0);

    /* At least one NPC in the starting maps can talk: the phase 1 path. */
    int talkers = 0;
    for (int m = 0; m < PC_MAP_COUNT; m++)
        for (int i = 0; i < pc_maps[m].object_count; i++)
            if (pc_maps[m].objects[i].text >= 0) talkers++;
    assert(talkers >= 10);

    /* ---- font ---- */
    assert(PC_FONT_TILES >= 100);
    for (int i = 0; i < PC_FONT_TILES; i++) {
        int ink = 0;
        for (int r = 0; r < 8; r++) ink += pc_font[i][r] != 0;
        assert(ink > 0);          /* no empty tile inside the used range */
    }
    /* 'A' at $80 is tile 0 and is not the same shape as 'B'. */
    int diff = 0;
    for (int r = 0; r < 8; r++) diff += (pc_font[0][r] != pc_font[1][r]);
    assert(diff > 0);
    assert(PC_FONT_COMMON_TILES == 6);

    /* ---- options ---- */
    /* 13 settings + Done, the counts the original's menus offer. The names and
     * every value are charmap bytes: a value that is not printable would print
     * as nothing at all on the handheld, which is how a shifted table would show
     * up. */
    static const int k_counts[PC_OPTION_COUNT] = { 4, 4, 20, 8, 2, 2, 2, 3, 3, 2, 2, 2, 2 };
    assert(PC_OPTION_COUNT == 13);
    for (int i = 0; i < PC_OPTION_COUNT; i++) {
        const PcOptionString *name = &pc_option_names[i];
        assert(name->len > 0 && name->len <= PC_OPTION_MAX_VALUES);
        const PcOptionDef *def = &pc_options[i];
        assert(def->count == k_counts[i]);
        if (i == PC_OPT_FRAME) {
            assert(def->values == 0);       /* the value is a number */
            continue;
        }
        assert(def->values);
        for (int v = 0; v < def->count; v++) {
            const PcOptionString *s = &def->values[v];
            assert(s->bytes && s->len > 0);
            for (int k = 0; k < s->len; k++)
                assert(s->bytes[k] == PCT_SPACE || font_glyph(s->bytes[k]));
        }
    }
    assert(pc_option_done.len == 4);

    printf("gen data: %d texts, %d maps, %d talkers, %d font tiles\n",
           PC_TEXT_COUNT, PC_MAP_COUNT, talkers, PC_FONT_TILES);
    return 0;
}

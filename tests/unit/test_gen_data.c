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
#include "gen/moves.h"
#include "gen/options.h"
#include "gen/pokemon.h"
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

    /* ---- species ---- */
    /* The ids are the original's (constants/pokemon_constants.asm): 1..291 with
     * the `const_skip` at 0x100 left empty, so a species id is never translated
     * and a gap can never hide a shifted table. */
    assert(PC_SPECIES_COUNT == 292);
    assert(PC_SPECIES_BULBASAUR == 1);
    assert(PC_SPECIES_PIKACHU == 25);
    assert(PC_SPECIES_MEWTWO == 150);
    assert(PC_SPECIES_CHIKORITA == 152);
    int species = 0;
    for (int i = 1; i < PC_SPECIES_COUNT; i++) {
        const PcSpecies *sp = &pc_species[i];
        if (sp->base[0] == 0) {
            assert(i == 256);            /* the only hole in the id space */
            continue;
        }
        species++;
        /* A name is charmap bytes inside the blob, never empty, never over the
         * ten glyphs a name box holds. */
        assert(sp->name + sp->name_len <= sizeof pc_species_names);
        assert(sp->name_len > 0 && sp->name_len <= 10);
        for (int k = 0; k < sp->name_len; k++) assert(pc_species_names[sp->name + k] != 0);
        assert(sp->type[0] < PC_TYPE_UNKNOWN_T);
        assert(sp->type[1] < PC_TYPE_UNKNOWN_T);
        for (int k = 0; k < 6; k++) assert(sp->base[k] > 0);
        assert(sp->catch_rate > 0);
        assert(sp->base_exp > 0);
        assert(sp->growth < PC_GROWTH_COUNT);
    }
    assert(species == 290);

    /* Three facts that would go wrong first if the extractor shifted rows:
     * Bulbasaur's stats and dual type, the repeated slot of a one-type species,
     * and the form rule (Rattata's plain form wins over its Alolan one). */
    const PcSpecies *bulba = &pc_species[PC_SPECIES_BULBASAUR];
    assert(bulba->name_len == 9);                        /* "Bulbasaur" */
    assert(bulba->base[0] == 45 && bulba->base[1] == 49 && bulba->base[4] == 65);
    assert(bulba->type[0] == PC_TYPE_GRASS && bulba->type[1] == PC_TYPE_POISON);
    assert(bulba->growth == PC_GROWTH_MEDIUM_SLOW);
    const PcSpecies *pika = &pc_species[PC_SPECIES_PIKACHU];
    assert(pika->type[0] == PC_TYPE_ELECTRIC && pika->type[1] == PC_TYPE_ELECTRIC);
    const PcSpecies *rattata = &pc_species[PC_SPECIES_RATTATA];
    assert(rattata->base[0] == 30 && rattata->base[1] == 56);
    assert(rattata->base[2] == 35 && rattata->base[3] == 72);
    assert(rattata->base[4] == 25 && rattata->base[5] == 35);

    /* ---- growth curves ---- */
    /* The four coefficient rows of data/growth_rates.asm, signed quadratic
     * included. A misread of the `-\3 | $80` sign would make Medium Slow the
     * fastest curve of all without moving any other table. */
    assert(PC_GROWTH_COUNT == 4);
    assert(pc_growth_rates[PC_GROWTH_MEDIUM_FAST].a == 1 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_FAST].b == 1 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_FAST].c == 0 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_FAST].d == 0 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_FAST].e == 0);
    assert(pc_growth_rates[PC_GROWTH_MEDIUM_SLOW].a == 6 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_SLOW].b == 5 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_SLOW].c == -15 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_SLOW].d == 100 &&
           pc_growth_rates[PC_GROWTH_MEDIUM_SLOW].e == 140);
    assert(pc_growth_rates[PC_GROWTH_FAST].a == 4 &&
           pc_growth_rates[PC_GROWTH_FAST].b == 5 &&
           pc_growth_rates[PC_GROWTH_FAST].c == 0 &&
           pc_growth_rates[PC_GROWTH_FAST].d == 0 &&
           pc_growth_rates[PC_GROWTH_FAST].e == 0);
    assert(pc_growth_rates[PC_GROWTH_SLOW].a == 5 &&
           pc_growth_rates[PC_GROWTH_SLOW].b == 4 &&
           pc_growth_rates[PC_GROWTH_SLOW].c == 0 &&
           pc_growth_rates[PC_GROWTH_SLOW].d == 0 &&
           pc_growth_rates[PC_GROWTH_SLOW].e == 0);

    /* ---- moves ---- */
    /* 255 moves after the build's conditionals are resolved; id 0 is "no move"
     * and the original's NUM_ATTACKS is 255, so the last id is Struggle. */
    assert(PC_MOVE_COUNT == 255);
    assert(PC_MOVE_TACKLE == 33);
    assert(PC_MOVE_STRUGGLE == PC_MOVE_COUNT);
    const PcMove *none = &pc_moves[0];
    assert(none->power == 0 && none->pp == 0 && none->effect == 0);
    for (int i = 1; i <= PC_MOVE_COUNT; i++) {
        const PcMove *mv = &pc_moves[i];
        assert(mv->name + mv->name_len <= sizeof pc_move_names);
        assert(mv->name_len > 0 && mv->name_len <= 12);
        /* UNKNOWN_T is a real type value here: Struggle carries it. */
        assert(mv->type <= PC_TYPE_UNKNOWN_T);
        assert(mv->category <= PC_CAT_STATUS);
        assert(mv->pp > 0 && mv->pp <= 64);
        /* -1 is the original's "never misses" (Swift, Struggle). */
        assert(mv->accuracy == -1 || (mv->accuracy > 0 && mv->accuracy <= 100));
        /* Only status moves may have no power, and they must have none. */
        if (mv->category == PC_CAT_STATUS) assert(mv->power == 0);
        else assert(mv->power > 0);
    }
    const PcMove *tackle = &pc_moves[PC_MOVE_TACKLE];
    assert(tackle->power == 40 && tackle->accuracy == 100 && tackle->pp == 35);
    assert(tackle->type == PC_TYPE_NORMAL && tackle->category == PC_CAT_PHYSICAL);
    assert(tackle->effect == 0);                         /* EFFECT_NORMAL_HIT */
    const PcMove *bolt = &pc_moves[PC_MOVE_THUNDERBOLT];
    assert(bolt->power == 90 && bolt->category == PC_CAT_SPECIAL);
    assert(bolt->type == PC_TYPE_ELECTRIC && bolt->chance == 10);

    printf("gen data: %d texts, %d maps, %d talkers, %d font tiles,"
           " %d species, %d moves\n",
           PC_TEXT_COUNT, PC_MAP_COUNT, talkers, PC_FONT_TILES, species,
           PC_MOVE_COUNT);
    return 0;
}

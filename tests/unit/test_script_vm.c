/* test_script_vm — the script VM against hand-built bytecode and real blobs.
 *
 * The hand-built cases pin each opcode's operands and skip behaviour; the last
 * two run bytecode the extractor assembled from New Bark Town's own .asm, which
 * is the only way to know the two halves agree about how a script is encoded.
 */
#include <stdio.h>
#include <string.h>

#include "gen/text.h"
#include "gen/world.h"
#include "gen/move.h"
#include "gen/scripts.h"
#include "script/vm.h"

static int failures;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
            failures++;                                                     \
        }                                                                   \
    } while (0)

/* Byte helpers so a script reads like the .asm it came from. */
static int b(uint8_t *o, int n, uint8_t v) { o[n++] = v; return n; }
static int w(uint8_t *o, int n, uint16_t v) {
    o[n++] = (uint8_t)(v & 0xff);
    o[n++] = (uint8_t)(v >> 8);
    return n;
}

/* flags/2 events/9: set, clear, test -------------------------------------- */
static void test_flags(void) {
    uint8_t code[16];
    int n = 0;
    n = b(code, n, PCS_SETFLAG);
    n = w(code, n, 9);
    n = b(code, n, PCS_CHECKFLAG);
    n = w(code, n, 9);
    n = b(code, n, PCS_IFFALSE);
    n = w(code, n, (uint16_t)n + 2 + 3);   /* skip a 1-byte script safely */
    n = b(code, n, PCS_CLEARFLAG);
    n = w(code, n, 9);
    n = b(code, n, PCS_END);

    uint8_t flags[2] = { 0, 0 };
    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, flags, sizeof flags);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "set/check/iftrue reaches end");
    CHECK(flags[1] == 0, "the flag was cleared before end");

    /* Same script, without the clear: the bit stays set. */
    n = 0;
    n = b(code, n, PCS_SETFLAG);
    n = w(code, n, 9);
    n = b(code, n, PCS_END);
    scr_vm_start(&vm, code, (uint16_t)n, 0, flags, sizeof flags);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "plain set ends");
    CHECK(flags[1] == 0x02, "flag 9 is bit 1 of byte 1");

    /* A flag outside the array is not an out-of-bounds write. */
    n = 0;
    n = b(code, n, PCS_SETFLAG);
    n = w(code, n, 300);
    n = b(code, n, PCS_CHECKFLAG);
    n = w(code, n, 300);
    n = b(code, n, PCS_END);
    scr_vm_start(&vm, code, (uint16_t)n, 0, flags, sizeof flags);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "far flag ends");
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "null flag table is allowed");
}

/* scall/jump/end --------------------------------------------------------- */
static void test_call(void) {
    uint8_t code[16];
    int n = 0;
    n = b(code, n, PCS_SCALL);
    int callee = n + 3;           /* past the operand and the `end` below */
    n = w(code, n, (uint16_t)callee);
    n = b(code, n, PCS_END);      /* returns here, then ends */
    n = b(code, n, PCS_END);      /* the call target: returns to the caller */

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "call then end at the top");
    CHECK(vm.sp == 0, "the stack unwinds");
}

/* ifequal + text --------------------------------------------------------- */
static void test_ifequal_and_text(void) {
    uint8_t code[24];
    int n = 0;
    n = b(code, n, PCS_IFEQUAL);
    n = b(code, n, 2);            /* compare with 2 */
    int text_at = n + 2 + 1;      /* operand, the `end`, then the showtext */
    n = w(code, n, (uint16_t)text_at);
    n = b(code, n, PCS_END);      /* not taken: end */
    n = b(code, n, PCS_SHOWTEXT);
    n = w(code, n, 42);

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    scr_vm_set_cond(&vm, 1);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "cond 1 skips the jump");
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    scr_vm_set_cond(&vm, 2);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT, "cond 2 yields an event");
    CHECK(out.kind == SCR_EV_TEXT && out.a == 42, "the event is the text");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_ERROR,
          "a text with no `end` runs off the blob");
}

/* faceplayer inside showtextfaceplayer: face, then the text ------------- */
static void test_face_then_text(void) {
    uint8_t code[8];
    int n = 0;
    n = b(code, n, PCS_SHOWTEXTFACEPLAYER);
    n = w(code, n, 7);

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT, "faces first");
    CHECK(out.kind == SCR_EV_FACE && out.a == SCR_OBJECT_LAST_TALKED,
          "faceplayer aims the last talked to at the player");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT, "then the text");
    CHECK(out.kind == SCR_EV_TEXT && out.a == 7, "the text index survives");
}

/* An opcode the port does not run yet: report it, then refuse to continue. */
static void test_unsupported(void) {
    uint8_t code[4];
    int n = 0;
    n = b(code, n, PCS_SPECIAL);
    n = b(code, n, 3);
    n = b(code, n, PCS_END);

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT, "unsupported yields");
    CHECK(out.kind == SCR_EV_UNSUPPORTED && out.a == PCS_SPECIAL,
          "the opcode is reported");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_ERROR, "and the vm stays dead");
}

/* Truncated operand: no read outside the blob. */
static void test_truncated(void) {
    uint8_t code[2];
    code[0] = PCS_SJUMP;
    code[1] = 0x00;               /* the high byte is missing */

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, 2, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_ERROR, "short operand is an error");
    CHECK(vm.dead != 0, "and the vm marks itself dead");

    /* A jump outside the blob is refused too. */
    uint8_t far[3] = { PCS_SJUMP, 0xff, 0x00 };
    scr_vm_start(&vm, far, 3, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_ERROR, "jump outside is an error");
}

/* ---------------------------------------------------------------- real data */
static const PcScriptMap *map_by_name(const char *name) {
    for (int i = 0; i < PC_SCRIPT_MAP_COUNT; i++) {
        if (strcmp(pc_script_maps[i].map, name) == 0) {
            return &pc_script_maps[i];
        }
    }
    return NULL;
}

static uint16_t label_offset(const PcScriptMap *map, const char *label) {
    for (int i = 0; i < map->label_count; i++) {
        if (strcmp(map->labels[i].label, label) == 0) {
            return map->labels[i].offset;
        }
    }
    return SCR_TEXT_NONE;
}

/* New Bark Town's NWMAP callback: set flag, clear event, endcallback. */
static void test_real_callback(void) {
    const PcScriptMap *map = map_by_name("NewBarkTown");
    CHECK(map != NULL, "New Bark Town has scripts");
    if (!map) {
        return;
    }
    uint16_t at = label_offset(map, "NewBarkTownFlyPoint");
    CHECK(at != SCR_TEXT_NONE, "the callback label exists");

    uint8_t flags[64] = { 0 };
    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, map->bytes, map->len, at, flags, sizeof flags);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "the callback runs to its end");
    int any = 0;
    for (int i = 0; i < (int)sizeof flags; i++) {
        any |= flags[i] != 0;
    }
    CHECK(any, "it set a flag");
}

/* The teacher's script: music, face, text, movement -- it must not fall over
 * before the part the port can run, and it must ask for the real text. */
static void test_real_script(void) {
    const PcScriptMap *map = map_by_name("NewBarkTown");
    if (!map) {
        return;
    }
    uint16_t at = label_offset(map, "NewBarkTown_TeacherStopsYouTrigger1");
    CHECK(at != SCR_TEXT_NONE, "the trigger label exists");

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, map->bytes, map->len, at, NULL, 0);
    ScrRun r = scr_vm_next(&vm, &out);
    CHECK(r == SCR_RUN_EVENT && out.kind == SCR_EV_SOUND,
          "it starts with playmusic");
    r = scr_vm_next(&vm, &out);
    CHECK(r == SCR_RUN_EVENT && out.kind == SCR_EV_FACE && out.b == 2,
          "then turns the teacher left (2)");
    r = scr_vm_next(&vm, &out);
    CHECK(r == SCR_RUN_EVENT && out.kind == SCR_EV_TEXT,
          "then shows the real text");
    CHECK(out.a < PC_TEXT_COUNT, "and the index is inside pc_texts");
}

/* `applyonemovement` carries its steps inline, so the VM has to walk them to
 * know where the script continues — including a `step_sleep` whose duration byte
 * happens to be `step_end`, which is exactly the byte that would cut the scan
 * short if the sleep's operand were not skipped. */
static void test_applyonemovement_inline(void) {
    uint8_t code[8];
    int n = 0;
    n = b(code, n, PCS_APPLYONEMOVEMENT);
    n = b(code, n, 1);                     /* person 1 */
    n = b(code, n, PCM_STEP_UP);           /* the step, at offset 2 */
    n = b(code, n, PCM_STEP_SLEEP);
    n = b(code, n, PCM_STEP_END);          /* the duration, not the end */
    n = b(code, n, PCM_STEP_END);          /* the real one */
    n = b(code, n, PCS_END);

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    ScrRun r = scr_vm_next(&vm, &out);
    CHECK(r == SCR_RUN_EVENT && out.kind == SCR_EV_MOVE, "it asks for a movement");
    CHECK(out.a == 2, "the offset is the first step byte");
    CHECK(out.b == 1, "the person is the one the script named");
    r = scr_vm_next(&vm, &out);
    CHECK(r == SCR_RUN_END, "and the script continues after the movement");
}

/* The teacher's first script, moved through by hand: the movement it applies is
 * the label the extractor kept in the same blob, and the teacher is object 3. */
static void test_real_movement(void) {
    const PcScriptMap *map = map_by_name("NewBarkTown");
    if (!map) {
        return;
    }
    uint16_t at = label_offset(map, "NewBarkTown_TeacherStopsYouTrigger1");
    uint16_t runs = label_offset(map, "Movement_TeacherRunsToYou1_NBT");
    uint16_t back = label_offset(map, "Movement_TeacherBringsYouBack1_NBT");
    CHECK(at != SCR_TEXT_NONE, "the trigger label exists");
    CHECK(runs != SCR_TEXT_NONE && runs < map->len,
          "its movement is an offset into the same blob");
    CHECK(back != SCR_TEXT_NONE && back < map->len,
          "so is the way back");
    if (runs == SCR_TEXT_NONE || back == SCR_TEXT_NONE) {
        return;
    }
    CHECK(map->bytes[runs] == PCM_STEP_LEFT, "Movement_TeacherRunsToYou1 starts left");
    CHECK(map->bytes[back] == PCM_STEP_RIGHT, "Movement_TeacherBringsYouBack1 starts right");

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, map->bytes, map->len, at, NULL, 0);
    int seen_runs = 0, seen_back = 0, seen_follow = 0, seen_stop = 0;
    for (int guard = 0; guard < 32; guard++) {
        ScrRun r = scr_vm_next(&vm, &out);
        if (r != SCR_RUN_EVENT) break;
        if (out.kind == SCR_EV_MOVE && out.a == runs) {
            CHECK(out.b == 3, "the teacher is object 3");
            seen_runs++;
        } else if (out.kind == SCR_EV_MOVE && out.a == back) {
            CHECK(out.b == 3, "still the teacher");
            seen_back++;
        } else if (out.kind == SCR_EV_FOLLOW) {
            CHECK(out.a == 3 && out.b == 0, "follow: teacher leads, the player follows");
            seen_follow++;
        } else if (out.kind == SCR_EV_STOPFOLLOW) {
            seen_stop++;
        } else if (out.kind == SCR_EV_UNSUPPORTED) {
            break;                     /* `special RestartMapMusic`, not ported */
        }
    }
    CHECK(seen_runs == 1, "the cutscene runs the teacher to the player once");
    CHECK(seen_back == 1, "and brings him back once");
    CHECK(seen_follow == 1, "with the player following in between");
    CHECK(seen_stop == 1, "and stops following before the last text");
}

/* `jumptext` is a jump, not a call: the script is over after its text, and the
 * bytes that follow are text, not code (here: an opcode that would be
 * unsupported, which proves it was never read). */
static void test_jumptext_ends(void) {
    uint8_t code[4] = { PCS_JUMPTHISTEXT, 0x01, 0x00, 0xaa };
    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, sizeof code, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT && out.kind == SCR_EV_TEXT &&
          out.a == 1, "jumpthistext shows its text");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "and then the script is over");

    /* The faceplayer variants turn first, then show the same text. */
    uint8_t face[3] = { PCS_JUMPTEXTFACEPLAYER, 0x02, 0x00 };
    scr_vm_start(&vm, face, sizeof face, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT && out.kind == SCR_EV_FACE,
          "jumptextfaceplayer turns to the player first");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT && out.kind == SCR_EV_TEXT &&
          out.a == 2, "then shows the text");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "and ends");
}

/* The objects' scripts are the ones the game runs now. 32 of the 40 objects in
 * the build have one; 25 reach a text and 7 stop at a command the port does not
 * run yet (`readvar`, `special`, battle). Where the extractor's flat guess and the
 * script agree it is because both read the same text; the five that differ are
 * scripts whose first text depends on an event (the teacher says one of three
 * things), which is exactly why the VM is the better oracle. */
static void test_object_scripts(void) {
    int with_script = 0, reached = 0, same = 0, outside = 0;
    for (int m = 0; m < PC_SCRIPT_MAP_COUNT; m++) {
        const PcScriptMap *sm = &pc_script_maps[m];
        const PcMapDef *def = NULL;
        for (int i = 0; i < PC_MAP_COUNT; i++) {
            if (i == sm->map_index) def = &pc_maps[i];
        }
        if (!def || !sm->objects) {
            continue;
        }
        for (int o = 0; o < sm->object_count && o < def->object_count; o++) {
            if (sm->objects[o] == 0xffff) {
                continue;
            }
            with_script++;
            ScrVm vm;
            ScrOut out;
            uint8_t flags[PC_SCRIPT_FLAG_BYTES] = { 0 };
            scr_vm_start(&vm, sm->bytes, sm->len, sm->objects[o], flags, sizeof flags);
            for (int step = 0; step < 64; step++) {
                ScrRun r = scr_vm_next(&vm, &out);
                if (r != SCR_RUN_EVENT || out.kind == SCR_EV_UNSUPPORTED) {
                    break;
                }
                if (out.kind != SCR_EV_TEXT) {
                    continue;
                }
                reached++;
                if (out.a >= PCS_TEXT_INDEX_COUNT) {
                    outside++;
                }
                same += out.a == def->objects[o].text;
                break;
            }
        }
    }
    CHECK(with_script == 32, "32 objects have a script of their own");
    CHECK(reached == 25, "25 of them reach a text before a command the port lacks");
    CHECK(outside == 0, "every text they show is inside pc_texts");
    CHECK(same == 20, "20 agree with the flat text the extractor read for them");
}

/* The commands a cutscene uses to talk and to place people: `writetext` prints
 * without waiting, `waitbutton` is the wait, `endtext` closes and ends, and
 * `moveobject` parks an object on another tile. */
static void test_cutscene_commands(void) {
    uint8_t code[24];
    int n = 0;
    n = b(code, n, PCS_WRITETEXT);
    n = w(code, n, 7);                 /* the text */
    n = b(code, n, PCS_WAITBUTTON);
    n = b(code, n, PCS_MOVEOBJECT);
    n = b(code, n, 3);                 /* person */
    n = b(code, n, 12);                /* x tile */
    n = b(code, n, 9);                 /* y tile */
    n = b(code, n, PCS_REANCHORMAP);
    n = b(code, n, PCS_ENDTEXT);

    ScrVm vm;
    ScrOut out;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT &&
          out.kind == SCR_EV_TEXT_NOWAIT && out.a == 7,
          "writetext prints without waiting");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT &&
          out.kind == SCR_EV_TEXT_WAIT, "waitbutton asks for the box to be read");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT && out.kind == SCR_EV_PLACE &&
          out.b == 3 && (out.a & 0xff) == 12 && (out.a >> 8) == 9,
          "moveobject carries person, x and y");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT &&
          out.kind == SCR_EV_TEXT_END, "endtext closes the box");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "and the script is over");

    /* `waitendtext` is the same wait, and ends too. */
    n = 0;
    n = b(code, n, PCS_WAITENDTEXT);
    n = b(code, n, PCS_WRITETEXT);     /* never reached */
    n = w(code, n, 1);
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT &&
          out.kind == SCR_EV_TEXT_WAIT, "waitendtext waits first");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "then the script ends");

    /* `reloadmap` stops the script: the map load owns what happens next. */
    n = 0;
    n = b(code, n, PCS_RELOADMAP);
    n = b(code, n, PCS_WRITETEXT);
    n = w(code, n, 1);
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT &&
          out.kind == SCR_EV_RELOADMAP, "reloadmap asks for the map back");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "and the script does not go on");
}

/* `sdefer` is not a command the caller has to run: it leaves a second script
 * behind, and the VM reports it once. */
static void test_sdefer(void) {
    uint8_t code[8];
    int n = 0;
    n = b(code, n, PCS_WRITETEXT);
    n = w(code, n, 2);
    n = b(code, n, PCS_SDEFER);
    n = w(code, n, 6);
    n = b(code, n, PCS_END);

    ScrVm vm;
    ScrOut out;
    uint16_t at = 0;
    scr_vm_start(&vm, code, (uint16_t)n, 0, NULL, 0);
    CHECK(scr_vm_deferred(&vm, &at) == 0, "nothing deferred before the command");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_EVENT &&
          out.kind == SCR_EV_TEXT_NOWAIT, "the text comes first");
    CHECK(scr_vm_next(&vm, &out) == SCR_RUN_END, "the script ends");
    CHECK(scr_vm_deferred(&vm, &at) == 1 && at == 6,
          "and leaves the deferred script at its offset");
    CHECK(scr_vm_deferred(&vm, &at) == 0, "reported only once");
}

/* `verbosegiveitem SILK_SCARF, iffalse_endtext` takes its item as an argument
 * and the rest of the call (`iffalse_endtext`) as *code*, emitted after the
 * item bytes. Route 29 is the map that used to be skipped for this shape. */
static void test_real_giveitem(void) {
    const PcScriptMap *map = map_by_name("Route29");
    CHECK(map != NULL, "Route 29 has scripts");
    if (!map) {
        return;
    }
    uint16_t at = label_offset(map, "TuscanyScript");
    uint16_t end = label_offset(map, "TuscanyNotTuesdayScript");
    CHECK(at != SCR_TEXT_NONE && end != SCR_TEXT_NONE,
          "TuscanyScript and the script after it exist");

    int hits = 0, inside = 0;
    for (uint16_t i = 0; i + 3 < map->len; i++) {
        if (map->bytes[i] == PCS_VERBOSEGIVEITEM && map->bytes[i + 1] == 0x79 &&
                map->bytes[i + 2] == 1 && map->bytes[i + 3] == PCS_IFFALSE_ENDTEXT) {
            hits++;
            inside = i >= at && i < end;
        }
    }
    CHECK(hits == 1, "the item, its quantity and the fail-safe are emitted once");
    CHECK(inside, "and they sit inside TuscanyScript");
}

int main(void) {
    test_flags();
    test_call();
    test_ifequal_and_text();
    test_face_then_text();
    test_unsupported();
    test_truncated();
    test_real_callback();
    test_real_script();
    test_real_giveitem();
    test_cutscene_commands();
    test_sdefer();
    test_jumptext_ends();
    test_object_scripts();
    test_applyonemovement_inline();
    test_real_movement();
    if (failures) {
        printf("script/vm: %d failure(s)\n", failures);
        return 1;
    }
    printf("script/vm: ok\n");
    return 0;
}

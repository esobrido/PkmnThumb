/*
 * script/vm.h — the original's script bytecode, interpreted.
 *
 * Polished Crystal's map scripts are not code that has to be translated: they
 * are a byte stream and a jump table (engine/overworld/scripting.asm). The port
 * copies the byte stream (tools/extract.py assembles it from the .asm macros)
 * and reimplements the jump table here, so a script written for the GB runs in
 * this engine with the same steps in the same order.
 *
 * The VM is pure: no Mote, no globals, no allocation. It owns its position and
 * its call stack (5 entries — the original's wScriptStackSize is 5) and it
 * cannot read outside the byte stream it was handed.
 *
 * Nothing here touches the world. When a command needs something done — print
 * this text, turn that object, play this sound — `scr_vm_next` returns and the
 * caller does it, then calls again. That keeps the VM testable without a
 * screen, and keeps game.c the only place that knows what a textbox is.
 */
#ifndef PC_SCRIPT_VM_H
#define PC_SCRIPT_VM_H

#include <stdint.h>

/* wScriptStackSize in the original. It has no overflow check (see docs/06);
 * here the sixth call overwrites the outermost entry instead of WRAM. */
#define SCR_STACK_MAX 5

/* A script that names a text it did not get from the extractor. */
#define SCR_TEXT_NONE 0xffff

/* Object ids in a script: the player is 0, and LAST_TALKED is -2 in the
 * original's constants, so it cannot collide with a real object. */
#define SCR_OBJECT_LAST_TALKED 0xfe
/* A facing the caller has to compute: "towards the player". */
#define SCR_FACE_TO_PLAYER 0xff

typedef enum {
    SCR_RUN_END = 0,   /* the script ended (or returned with no caller) */
    SCR_RUN_EVENT,     /* `out` holds work for the caller */
    SCR_RUN_ERROR,     /* bytecode ran off the end, or the VM is dead */
} ScrRun;

typedef enum {
    SCR_EV_TEXT = 1,     /* a = index into pc_texts */
    SCR_EV_TEXT_OPEN,    /* open the box; nothing to show yet */
    SCR_EV_TEXT_CLOSE,   /* close the box */
    SCR_EV_FACE,         /* a = object id, b = facing */
    SCR_EV_SOUND,        /* a = sound id */
    SCR_EV_DELAY,        /* a = frames */
    SCR_EV_SPRITE,       /* a = object id, b = 1 to show, 0 to hide */
    SCR_EV_UNSUPPORTED,  /* a = opcode; the script cannot continue */
} ScrEvent;

typedef struct {
    uint8_t kind;   /* ScrEvent */
    uint16_t a;     /* text index, object id, sound id, frames or opcode */
    uint8_t b;
} ScrOut;

typedef struct {
    const uint8_t *code;
    uint16_t len;
    uint16_t pos;
    uint16_t stack[SCR_STACK_MAX];
    uint8_t sp;
    uint8_t cond;      /* hScriptVar: what iftrue/iffalse/ifequal test */
    uint8_t dead;      /* hit an opcode this port cannot run */
    uint8_t *flags;    /* engine flags and map events, one bit each */
    uint16_t nflags;
    uint16_t pending;  /* text owed after a `faceplayer` in a showtextfaceplayer */
    uint8_t ending;    /* a `jumptext` was the last command: end after its text */
} ScrVm;

/* `at` is an offset inside `code` — a label offset from gen/scripts.h.
 * `flags` may be NULL, in which case flag tests read as 0 and writes are
 * dropped; that is what the tests use for scripts that never touch a flag. */
void scr_vm_start(ScrVm *vm, const uint8_t *code, uint16_t len, uint16_t at,
                  uint8_t *flags, uint16_t nflags);

/* The script's condition register, set by the caller before a step: it is the
 * result of the check the game just ran (checkevent, checkitem, …). */
void scr_vm_set_cond(ScrVm *vm, uint8_t value);

/* The shared flag/event bit array, for the work the engine does outside a
 * script: a bg_event tests its flag before it reads (that is what turns a taken
 * hidden item into an empty cell) and sets it when the item is picked up. Ids
 * outside the array read as 0 and writes are dropped, so a caller never has to
 * bounds-check first. */
uint8_t scr_flag_get(const uint8_t *flags, uint16_t nflags, uint16_t id);
void scr_flag_set(uint8_t *flags, uint16_t nflags, uint16_t id, int on);

/* Run until the script ends or needs something done. */
ScrRun scr_vm_next(ScrVm *vm, ScrOut *out);

#endif

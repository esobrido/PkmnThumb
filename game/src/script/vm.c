/*
 * script/vm.c — RunScriptCommand's jump table, rewritten in C.
 *
 * Every opcode handled here has a `Script_*` twin in
 * engine/overworld/scripting.asm; the comments name it so the two can be read
 * side by side. Commands whose effect needs a system the port does not have yet
 * (battle, specials, item balls, scenes) are not guessed at: the VM reports the
 * opcode as unsupported and stops, and the caller decides what to do.
 */
#include "script/vm.h"

#include "gen/scripts.h"

/* The original's GetScriptByte. A short read is not a crash: the VM marks
 * itself dead and the caller sees SCR_RUN_ERROR. */
static uint8_t byte(ScrVm *vm) {
    if (vm->pos >= vm->len) {
        vm->dead = 1;
        return 0xff;
    }
    return vm->code[vm->pos++];
}

/* GetScriptWord: little-endian, as `dw` emits it. */
static uint16_t word(ScrVm *vm) {
    uint8_t lo = byte(vm);
    uint8_t hi = byte(vm);
    return (uint16_t)(lo | (hi << 8));
}

static int jump(ScrVm *vm, uint16_t at) {
    if (at >= vm->len) {
        vm->dead = 1;
        return 0;
    }
    vm->pos = at;
    return 1;
}

static uint8_t flag_get(const ScrVm *vm, uint16_t id) {
    if (!vm->flags || id >= (uint16_t)(vm->nflags * 8u)) {
        return 0;
    }
    return (uint8_t)((vm->flags[id >> 3] >> (id & 7)) & 1);
}

static void flag_set(ScrVm *vm, uint16_t id, int on) {
    if (!vm->flags || id >= (uint16_t)(vm->nflags * 8u)) {
        return;
    }
    if (on) {
        vm->flags[id >> 3] |= (uint8_t)(1u << (id & 7));
    } else {
        vm->flags[id >> 3] &= (uint8_t)~(1u << (id & 7));
    }
}

void scr_vm_start(ScrVm *vm, const uint8_t *code, uint16_t len, uint16_t at,
                  uint8_t *flags, uint16_t nflags) {
    vm->code = code;
    vm->len = len;
    vm->pos = at;
    vm->sp = 0;
    vm->cond = 0;
    vm->dead = (uint8_t)(at >= len);
    vm->flags = flags;
    vm->nflags = nflags;
    vm->pending = SCR_TEXT_NONE;
    for (int i = 0; i < SCR_STACK_MAX; i++) {
        vm->stack[i] = 0;
    }
}

void scr_vm_set_cond(ScrVm *vm, uint8_t value) {
    vm->cond = value;
}

static ScrRun event(ScrOut *out, uint8_t kind, uint16_t a, uint8_t b) {
    out->kind = kind;
    out->a = a;
    out->b = b;
    return SCR_RUN_EVENT;
}

ScrRun scr_vm_next(ScrVm *vm, ScrOut *out) {
    if (!vm || !vm->code || !out) {
        return SCR_RUN_ERROR;
    }
    out->kind = 0;
    out->a = 0;
    out->b = 0;
    if (vm->dead) {
        return SCR_RUN_ERROR;
    }
    if (vm->pending != SCR_TEXT_NONE) {
        /* A `showtextfaceplayer` that turned to the player on the last call. */
        uint16_t text = vm->pending;
        vm->pending = SCR_TEXT_NONE;
        return event(out, SCR_EV_TEXT, text, 0);
    }

    for (;;) {
        if (vm->pos >= vm->len) {
            vm->dead = 1;
            return SCR_RUN_ERROR;
        }
        uint8_t op = vm->code[vm->pos++];
        switch (op) {
        case PCS_SCALL:            /* Script_scall: absolute call in this bank */
            if (vm->sp >= SCR_STACK_MAX) {
                /* The original overwrites the high byte of the oldest entry
                 * instead of checking; wrapping keeps the script running. */
                vm->sp = 0;
            }
            vm->stack[vm->sp++] = (uint16_t)(vm->pos + 2);
            if (!jump(vm, word(vm))) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_SJUMP:            /* Script_sjump */
            if (!jump(vm, word(vm))) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_IFEQUAL:          /* Script_ifequal: byte, pointer */
            {
                uint8_t value = byte(vm);
                uint16_t target = word(vm);
                if (vm->dead) {
                    return SCR_RUN_ERROR;
                }
                if (vm->cond == value && !jump(vm, target)) {
                    return SCR_RUN_ERROR;
                }
            }
            break;

        case PCS_IFFALSE:          /* Script_iffalse */
            uint16_t at = word(vm);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            if (vm->cond == 0 && !jump(vm, at)) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_IFTRUE:           /* Script_iftrue */
            at = word(vm);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            if (vm->cond != 0 && !jump(vm, at)) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_CHECKFLAG:        /* Script_checkflag: writes hScriptVar */
        case PCS_CHECKEVENT:
            vm->cond = flag_get(vm, word(vm));
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_SETFLAG:
        case PCS_SETEVENT:
            flag_set(vm, word(vm), 1);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_CLEARFLAG:
        case PCS_CLEAREVENT:
            flag_set(vm, word(vm), 0);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            break;

        /* --- text ------------------------------------------------------- */
        case PCS_SHOWTEXTFACEPLAYER:   /* face first, then the same as showtext */
            vm->pending = word(vm);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            return event(out, SCR_EV_FACE, SCR_OBJECT_LAST_TALKED,
                         SCR_FACE_TO_PLAYER);

        case PCS_SHOWTEXT:         /* Script_showtext: open, write, wait */
            at = word(vm);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            return event(out, SCR_EV_TEXT, at, 0);

        case PCS_OPENTEXT:         /* Script_opentext */
            return event(out, SCR_EV_TEXT_OPEN, 0, 0);

        case PCS_CLOSETEXT:        /* Script_closetext */
            return event(out, SCR_EV_TEXT_CLOSE, 0, 0);

        /* --- world ------------------------------------------------------ */
        case PCS_TURNOBJECT:       /* Script_turnobject: person, facing */
            {
                uint8_t who = byte(vm);
                uint8_t dir = byte(vm);
                if (vm->dead) {
                    return SCR_RUN_ERROR;
                }
                return event(out, SCR_EV_FACE, who, dir);
            }

        case PCS_FACEPLAYER:       /* Script_faceplayer */
            return event(out, SCR_EV_FACE, SCR_OBJECT_LAST_TALKED,
                         SCR_FACE_TO_PLAYER);

        case PCS_PLAYMUSIC:        /* Script_playmusic: sound id */
        case PCS_PLAYSOUND:
            {
                uint8_t id = byte(vm);
                if (vm->dead) {
                    return SCR_RUN_ERROR;
                }
                return event(out, SCR_EV_SOUND, id, 0);
            }

        case PCS_WAITSFX:          /* Script_waitsfx: the sound is synchronous */
            break;

        case PCS_PAUSE:            /* Script_pause: delay, A/B skips it */
            {
                uint8_t frames = byte(vm);
                if (vm->dead) {
                    return SCR_RUN_ERROR;
                }
                return event(out, SCR_EV_DELAY, frames, 0);
            }

        case PCS_SETLASTTALKED:    /* Script_setlasttalked: nothing to store */
            (void)byte(vm);
            if (vm->dead) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_DISAPPEAR:        /* person id; 0 hides, 1 shows */
        case PCS_APPEAR:
            {
                uint8_t who = byte(vm);
                if (vm->dead) {
                    return SCR_RUN_ERROR;
                }
                return event(out, SCR_EV_SPRITE, who, (uint8_t)(op == PCS_APPEAR));
            }

        /* --- end -------------------------------------------------------- */
        case PCS_END:              /* Script_end: return, or stop at the top */
            if (vm->sp == 0) {
                return SCR_RUN_END;
            }
            vm->sp--;
            if (!jump(vm, vm->stack[vm->sp])) {
                return SCR_RUN_ERROR;
            }
            break;

        case PCS_ENDCALLBACK:      /* Script_endcallback: a map callback */
        case PCS_ENDALL:
            return SCR_RUN_END;

        default:
            vm->dead = 1;
            return event(out, SCR_EV_UNSUPPORTED, op, 0);
        }
    }
}

#include "generator_gesture.h"

static void clear(void *ptr, unsigned n) {
    uint8_t *p=ptr;
    for (unsigned i=0;i<n;++i) p[i]=0;
}
int gen_gesture_init(GenGesture *g, unsigned active) {
    if (!g || active>1) return 0;
    clear(g,sizeof(*g)); g->active=(uint8_t)active; return 1;
}
void gen_gesture_clear_holds(GenGesture *g) {
    g->held=0; clear(g->owners,sizeof(g->owners));
}
void gen_gesture_cancel_prefix(GenGesture *g) {
    if (g->owners[GG_CHORD].kind==GG_OWNER_PREFIX)
        g->owners[GG_CHORD].flags |= GG_CANCELLED;
}
static unsigned held(const GenGesture *g, unsigned button) {
    return (g->held>>button)&1u;
}
static unsigned other_held(const GenGesture *g) {
    return held(g,GG_SHIFT)||held(g,GG_STOP)||held(g,GG_PLAY);
}
static uint8_t entry_reason(const GenGestureContext *c, const GenGesture *g) {
    if (!c || !(c->known&GG_K_SEQ) || c->seq!=1) return GG_SEQ_REQUIRED;
    if (!(c->known&GG_K_PAGE) || (c->page!=GG_PAGE_CHORD && c->page!=GG_PAGE_CC)) return GG_PAGE_INELIGIBLE;
    if (!(c->known&GG_K_TRANSPORT) || c->transport!=GG_STOPPED) return GG_TRANSPORT_NOT_STOPPED;
    if (!(c->known&GG_K_RECORD) || c->record_idle!=1) return GG_RECORD_BUSY;
    if (!(c->known&GG_K_PENDING) || c->pending!=0) return GG_TRANSITION_PENDING;
    if (!(c->known&GG_K_NOTES) || c->notes_held!=0) return GG_NOTES_HELD;
    if (!(c->known&GG_K_LENGTH) || c->length_pending!=0) return GG_LENGTH_PENDING;
    if (!(c->known&GG_K_CAPTURE) || c->capture!=0) return GG_CHORD_CAPTURE_ACTIVE;
    if (other_held(g)) return GG_OTHER_BUTTON_HELD;
    return GG_OK;
}
static uint8_t exit_reason(const GenGestureContext *c, const GenGesture *g) {
    if (!c || !(c->known&GG_K_TRANSPORT) || c->transport!=GG_STOPPED) return GG_TRANSPORT_NOT_STOPPED;
    if (!(c->known&GG_K_PENDING) || c->pending!=0) return GG_TRANSITION_PENDING;
    if (!(c->known&GG_K_NOTES) || c->notes_held!=0) return GG_NOTES_HELD;
    if (other_held(g)) return GG_OTHER_BUTTON_HELD;
    return GG_OK;
}
static void emit(GenGestureEffects *e, unsigned type, unsigned value, unsigned button, unsigned down) {
    /* Each branch below emits at most rearm + finish-hold + mode. */
    GenGestureEffect *r=&e->items[e->count++];
    r->type=(uint8_t)type; r->value=(uint8_t)value;
    r->button=(uint8_t)button; r->down=(uint8_t)down;
}
int gen_gesture_route(GenGesture *g, unsigned button, unsigned down,
                      const GenGestureContext *context, GenGestureEffects *effects) {
    if (!g || !effects || button>=GG_BUTTON_COUNT || down>1) return 0;
    clear(effects,sizeof(*effects));
    if (held(g,button)==down) return 1;
    unsigned combo_locked=(held(g,GG_CHORD)&&g->owners[GG_CHORD].kind==GG_OWNER_COMBO)||
                          (held(g,GG_RECORD)&&g->owners[GG_RECORD].kind==GG_OWNER_COMBO);
    unsigned attempt=down && button==GG_RECORD && !held(g,GG_SHIFT) &&
                     held(g,GG_CHORD) && g->owners[GG_CHORD].kind==GG_OWNER_PREFIX;
    if (button!=GG_CHORD && !attempt) gen_gesture_cancel_prefix(g);
    emit(effects,GG_EFFECT_REARM,0,0,0);
    if (down) {
        GenGestureOwner owner={0,0,0,0};
        unsigned shift=held(g,GG_SHIFT);
        if (combo_locked && (button==GG_CHORD || button==GG_RECORD)) owner.kind=GG_OWNER_COMBO;
        else if (attempt) {
            GenGestureOwner *prefix=&g->owners[GG_CHORD];
            uint8_t reason=prefix->reason;
            if (!reason && (prefix->flags&GG_CANCELLED)) reason=GG_PREFIX_CANCELLED;
            if (!reason && !!(prefix->flags&GG_STARTED_ACTIVE)!=g->active) reason=GG_MODE_CHANGED;
            if (!reason) reason=g->active?exit_reason(context,g):entry_reason(context,g);
            if (reason) {
                prefix->flags |= GG_ATTEMPTED|GG_CANCELLED;
                prefix->reason=reason; owner.kind=GG_OWNER_REJECTED;
                emit(effects,GG_EFFECT_REJECT,reason,0,0);
            } else {
                if (prefix->flags&GG_NATIVE_PRESS) emit(effects,GG_EFFECT_FINISH_HOLD,0,0,0);
                g->active=(uint8_t)!(prefix->flags&GG_STARTED_ACTIVE);
                clear(prefix,sizeof(*prefix)); prefix->kind=GG_OWNER_COMBO;
                owner.kind=GG_OWNER_COMBO;
                emit(effects,GG_EFFECT_MODE,g->active,0,0);
            }
        } else if (button==GG_SHIFT) owner.kind=GG_OWNER_NATIVE;
        else if (button==GG_CHORD && !g->held) {
            owner.kind=GG_OWNER_PREFIX;
            owner.flags=g->active?GG_STARTED_ACTIVE:GG_NATIVE_PRESS;
            owner.reason=g->active?exit_reason(context,g):entry_reason(context,g);
        } else if (!g->active) owner.kind=GG_OWNER_NATIVE;
        else if (button==GG_RECORD) {
            owner.kind=GG_OWNER_COMMAND; owner.action=shift?GG_COMMIT:GG_VARY;
        } else if (button==GG_STOP) {
            if (shift) { owner.kind=GG_OWNER_COMMAND; owner.action=GG_RESET; }
            else { owner.kind=GG_OWNER_TRANSPORT; emit(effects,GG_EFFECT_TRANSPORT,GG_TRANSPORT_STOP,0,0); }
        } else if (button==GG_PLAY) {
            if (shift) { owner.kind=GG_OWNER_RESERVED; owner.action=GG_HINT_UNASSIGNED; }
            else { owner.kind=GG_OWNER_TRANSPORT; emit(effects,GG_EFFECT_TRANSPORT,GG_TRANSPORT_TOGGLE,0,0); }
        } else {
            owner.kind=GG_OWNER_RESERVED; owner.action=shift?GG_HINT_UNASSIGNED:GG_HINT_GEN;
        }
        g->owners[button]=owner;
        if (owner.kind==GG_OWNER_NATIVE || (owner.kind==GG_OWNER_PREFIX && (owner.flags&GG_NATIVE_PRESS)))
            emit(effects,GG_EFFECT_NATIVE,0,button,1);
        g->held |= (uint8_t)(1u<<button);
    } else {
        GenGestureOwner owner=g->owners[button];
        clear(&g->owners[button],sizeof(owner));
        g->held &= (uint8_t)~(1u<<button);
        switch (owner.kind) {
        case GG_OWNER_NATIVE: emit(effects,GG_EFFECT_NATIVE,0,button,0); break;
        case GG_OWNER_PREFIX:
            if (owner.flags&GG_NATIVE_PRESS) emit(effects,GG_EFFECT_NATIVE,0,button,0);
            else if (!(owner.flags&GG_ATTEMPTED)) emit(effects,GG_EFFECT_HINT,GG_HINT_GEN,0,0);
            break;
        case GG_OWNER_COMMAND: emit(effects,GG_EFFECT_COMMAND,owner.action,0,0); break;
        case GG_OWNER_RESERVED: emit(effects,GG_EFFECT_HINT,owner.action,0,0); break;
        default: break;
        }
    }
    return 1;
}

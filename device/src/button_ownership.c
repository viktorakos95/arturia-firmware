#include "button_ownership.h"

static unsigned bit(unsigned down) { return down ? BO_DOWN_BIT : BO_UP_BIT; }
static unsigned call_bit(unsigned edge_down, unsigned native_down) {
    return 1u << (edge_down * 2u + native_down);
}
static int matches(const BoButton *b, BoTicket t) {
    return b->has_pair && t.generation == b->generation;
}
static BoTicket ticket(const BoButton *b, unsigned id, unsigned down) {
    BoTicket t = {b->generation, (uint8_t)id, (uint8_t)down}; return t;
}
static unsigned policy(const BoButton *b, unsigned down) {
    return down ? b->down_policy : b->up_policy;
}
static int router_quiet(const BoButton *b) {
    /* A tentative Chord pair with no positive native ack never reached router. */
    if (b->defer_chord && !b->chord_ack) return 1;
    return b->route_resolved == (BO_DOWN_BIT | BO_UP_BIT);
}
static int can_retire(const BoButton *b) {
    if (b->physical_down || b->in_flight || b->native_held || !router_quiet(b)) return 0;
    /* A native pair still owes a native UP callback (or explicit idle proof).
     * Suppressed pairs need no pretend native completion. Their generation is
     * retained as a tombstone until the next physical DOWN. */
    if (!b->force_suppress && (b->down_policy == BO_PASS_NATIVE || b->up_policy == BO_PASS_NATIVE))
        return !!(b->native_done & BO_UP_BIT);
    return 1;
}

void bo_init(BoState *s) {
    for (unsigned i=0;i<BO_BUTTON_COUNT;++i) {
        BoButton zero = {0}; s->buttons[i] = zero;
    }
}

BoDecision bo_sync(BoState *s, unsigned id, unsigned down) {
    if (id>=BO_BUTTON_COUNT || down>1) return BO_INVALID;
    BoButton *b=&s->buttons[id];
    if (b->initialized) return BO_INVALID;
    b->initialized=1; b->physical_down=(uint8_t)down; b->bootstrap_debt=1;
    return BO_NONE;
}

BoDecision bo_ack_initial_idle(BoState *s, unsigned id, unsigned idle_proven) {
    if (id>=BO_BUTTON_COUNT || idle_proven!=1) return BO_INVALID;
    BoButton *b=&s->buttons[id];
    if (!b->initialized || b->has_pair) return BO_INVALID;
    if (b->physical_down) return BO_WAIT_DEBT;
    b->bootstrap_debt=0; b->blocked=0;
    return BO_NONE;
}

BoDecision bo_current_ticket(const BoState *s, unsigned id, BoTicket *out) {
    if (id>=BO_BUTTON_COUNT || !out) return BO_INVALID;
    const BoButton *b=&s->buttons[id];
    if (!b->initialized || !b->has_pair) return BO_NONE;
    *out=ticket(b,id,b->physical_down);
    return b->blocked ? BO_WAIT_DEBT : BO_NONE;
}

BoDecision bo_observe(BoState *s, unsigned id, unsigned down, unsigned defer_chord, BoTicket *out) {
    if (id>=BO_BUTTON_COUNT || down>1 || defer_chord>1 || !out ||
        (defer_chord && (!down || id!=BO_CHORD_ID))) return BO_INVALID;
    BoButton *b=&s->buttons[id];
    if (!b->initialized) return BO_WAIT_DEBT;
    if (b->physical_down==down) {
        if (b->has_pair) *out=ticket(b,id,down);
        return b->blocked ? BO_WAIT_DEBT : BO_NONE;
    }
    if (down) {
        if (b->bootstrap_debt || (b->has_pair && !can_retire(b)) || b->generation==UINT32_MAX) {
            b->physical_down=1; b->blocked=1;
            if (b->has_pair) *out=ticket(b,id,down);
            return BO_WAIT_DEBT;
        }
        uint32_t next=b->generation+1;
        BoButton fresh={0}; *b=fresh;
        b->generation=next; b->initialized=1; b->has_pair=1; b->physical_down=1;
        b->defer_chord=(uint8_t)defer_chord;
        if (defer_chord) b->down_policy=b->up_policy=BO_PASS_NATIVE;
    } else {
        b->physical_down=0;
        if (!b->has_pair) return BO_NONE; /* Release of initial held sample. */
        b->up_seen=1;
    }
    *out=ticket(b,id,down);
    return b->blocked ? BO_WAIT_DEBT : BO_NONE;
}

BoDecision bo_take_route(BoState *s, unsigned id, BoRoute *out) {
    if (id>=BO_BUTTON_COUNT || !out) return BO_INVALID;
    BoButton *b=&s->buttons[id];
    out->decision=BO_NONE; out->native_effect_already_done=0;
    if (!b->has_pair) return BO_NONE;
    if (b->blocked) { out->decision=BO_WAIT_DEBT; return BO_WAIT_DEBT; }
    if (b->defer_chord && !b->chord_ack) return BO_NONE;
    unsigned down=!(b->route_resolved & BO_DOWN_BIT);
    unsigned mask=bit(down);
    if (!down && !b->up_seen) return BO_NONE;
    if (b->route_resolved & mask) return BO_NONE;
    if (b->route_taken & mask) { out->decision=BO_WAIT_DEBT; return BO_WAIT_DEBT; }
    b->route_taken|=(uint8_t)mask;
    out->ticket=ticket(b,id,down);
    out->native_effect_already_done=(uint8_t)(down && b->defer_chord && b->chord_ack);
    out->decision=down ? BO_ROUTE_PRESS : BO_ROUTE_RELEASE;
    return out->decision;
}

BoDecision bo_route_resolve(BoState *s, BoTicket t, BoDecision native_policy) {
    if (t.id>=BO_BUTTON_COUNT || t.down>1 ||
        (native_policy!=BO_PASS_NATIVE && native_policy!=BO_SUPPRESS)) return BO_INVALID;
    BoButton *b=&s->buttons[t.id]; unsigned mask=bit(t.down);
    if (!matches(b,t) || !(b->route_taken&mask) || (b->route_resolved&mask)) return BO_INVALID;
    if (t.down && b->defer_chord && b->chord_ack && native_policy!=BO_PASS_NATIVE) return BO_INVALID;
    if (!t.down && (b->force_suppress || b->down_policy==BO_SUPPRESS) && native_policy!=BO_SUPPRESS)
        return BO_INVALID;
    if (t.down) b->down_policy=(uint8_t)native_policy;
    else b->up_policy=(uint8_t)native_policy;
    b->route_resolved|=(uint8_t)mask;
    return BO_NONE;
}

BoDecision bo_native_pre(BoState *s, BoTicket t, unsigned native_down, BoNativeCall *out) {
    if (t.id>=BO_BUTTON_COUNT || t.down>1 || native_down>1 || !out) return BO_INVALID;
    BoButton *b=&s->buttons[t.id];
    if (!matches(b,t)) return b->has_pair && t.generation<b->generation ? BO_SUPPRESS : BO_INVALID;
    if (b->blocked || b->in_flight) return BO_WAIT_DEBT;
    if (b->force_suppress) return BO_SUPPRESS;
    if (!t.down && !b->up_seen) return BO_INVALID;
    /* Matching UP completion/explicit idle closes every native side, including
     * a late synthetic DOWN attached to the same physical UP ticket. */
    if (b->native_done&BO_UP_BIT) return BO_SUPPRESS;
    unsigned selected=policy(b,t.down);
    unsigned mask=bit(t.down);
    int tentative=b->defer_chord && !b->chord_ack;
    if (!tentative && !(b->route_resolved&mask)) return BO_WAIT_DEBT;
    if (selected==BO_SUPPRESS) return BO_SUPPRESS;
    if (selected!=BO_PASS_NATIVE) return BO_WAIT_DEBT;
    /* Matching native side runs once. Synthetic B+9 side is kept native only
     * for a PASS pair; it never obtains another physical router delivery. */
    if (b->call_done&call_bit(t.down,native_down)) return BO_SUPPRESS;
    if (b->call_serial==UINT32_MAX) return BO_WAIT_DEBT;
    ++b->call_serial;
    b->in_flight=1; b->in_flight_edge_down=t.down; b->in_flight_native_down=(uint8_t)native_down;
    out->edge=t; out->serial=b->call_serial; out->native_down=(uint8_t)native_down;
    return BO_PASS_NATIVE;
}

BoDecision bo_native_post(BoState *s, BoNativeCall call, BoNativeProof p) {
    BoTicket t=call.edge;
    if (t.id>=BO_BUTTON_COUNT || t.down>1 || call.native_down>1 || p.effects_done>1 ||
        p.normal_chord_path_seen>1) return BO_INVALID;
    BoButton *b=&s->buttons[t.id];
    if (!matches(b,t) || !b->in_flight || call.serial!=b->call_serial ||
        t.down!=b->in_flight_edge_down || call.native_down!=b->in_flight_native_down) return BO_INVALID;
    b->in_flight=0;
    b->call_done|=(uint8_t)call_bit(t.down,call.native_down);
    if (call.native_down==t.down) b->native_done|=(uint8_t)bit(t.down);
    if (p.effects_done) b->native_held=call.native_down;
    if (b->defer_chord && t.id==BO_CHORD_ID && t.down && call.native_down &&
        b->physical_down && !b->up_seen && p.effects_done && p.normal_chord_path_seen &&
        p.chord_d==1 && p.shift_m==0 && p.service_mode==0) b->chord_ack=1;
    return BO_NONE;
}

BoDecision bo_ack_finished_chord_hold(BoState *s, BoTicket t, unsigned observed_d_zero) {
    if (t.id!=BO_CHORD_ID || t.down!=1 || observed_d_zero!=1) return BO_INVALID;
    BoButton *b=&s->buttons[t.id];
    if (!matches(b,t) || b->in_flight || b->blocked || !b->physical_down || b->up_seen || !b->chord_ack ||
        !b->native_held || !(b->route_resolved&BO_DOWN_BIT)) return BO_INVALID;
    b->native_held=0; b->force_suppress=1;
    return BO_NONE;
}

BoDecision bo_ack_native_idle(BoState *s, BoTicket t, unsigned idle_proven) {
    if (t.id>=BO_BUTTON_COUNT || t.down>1 || idle_proven!=1) return BO_INVALID;
    BoButton *b=&s->buttons[t.id];
    if (!matches(b,t) || b->physical_down || b->in_flight) return BO_WAIT_DEBT;
    b->native_held=0; b->native_done|=BO_UP_BIT;
    b->blocked=0;
    return BO_NONE;
}

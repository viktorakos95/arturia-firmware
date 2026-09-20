#include "button_admission.h"

void ba_init(BaState *s) {
    BaState empty={0}; *s=empty;
}

BaDecision ba_take_route(BaState *s, BoState *ownership, unsigned id, BaRoute *out) {
    if (!s || !ownership || !out || id>=BO_BUTTON_COUNT) return BA_INVALID;
    GenGestureEffect empty={0}; out->feedback=empty;
    const BoButton *chord=&ownership->buttons[BO_CHORD_ID];
    int native_only=chord->has_pair && s->native_only_chord &&
        s->native_only_chord_generation==chord->generation;
    const BoButton *record=&ownership->buttons[BA_RECORD_ID];
    int force_reject=0;
    if (id==BA_RECORD_ID && record->has_pair && !(record->route_resolved&BO_DOWN_BIT) &&
        s->waiting_record && s->waiting_record_generation==record->generation) {
        force_reject=!chord->physical_down || chord->blocked ||
            chord->generation!=s->waiting_chord_generation;
    }
    if (id==BA_RECORD_ID && record->has_pair && !(record->route_resolved&BO_DOWN_BIT) &&
        !force_reject && chord->physical_down && chord->defer_chord && chord->chord_ack && !native_only &&
        !(chord->route_resolved&BO_DOWN_BIT)) {
        s->waiting_record=1; s->waiting_record_generation=record->generation;
        s->waiting_chord_generation=chord->generation;
        out->route.decision=BO_WAIT_DEBT; out->route.native_effect_already_done=0;
        return BA_WAIT_DEBT;
    }
    BoDecision decision=bo_take_route(ownership,id,&out->route);
    if (decision==BO_NONE) return BA_NONE;
    if (decision==BO_WAIT_DEBT) return BA_WAIT_DEBT;
    if (decision!=BO_ROUTE_PRESS && decision!=BO_ROUTE_RELEASE) return BA_INVALID;
    if (id==BO_CHORD_ID && native_only) {
        if (bo_route_resolve(ownership,out->route.ticket,BO_PASS_NATIVE)!=BO_NONE) return BA_INVALID;
        return BA_NATIVE_ONLY;
    }
    if (id!=BA_RECORD_ID) return BA_ROUTE_GESTURE;

    BoTicket t=out->route.ticket;
    if (t.down) {
        s->rejected_record=0; s->waiting_record=0;
        if (force_reject || (chord->physical_down &&
            (native_only || chord->bootstrap_debt || !chord->has_pair || chord->blocked ||
             (chord->defer_chord && !chord->chord_ack)))) {
            if (bo_route_resolve(ownership,t,BO_SUPPRESS)!=BO_NONE) return BA_INVALID;
            s->rejected_record=1; s->rejected_record_generation=t.generation;
            if (chord->has_pair && chord->defer_chord && !(chord->route_resolved&BO_DOWN_BIT)) {
                s->native_only_chord=1; s->native_only_chord_generation=chord->generation;
            }
            out->feedback.type=GG_EFFECT_REJECT;
            out->feedback.value=GG_TRANSITION_PENDING;
            return BA_REJECTED_COMBO;
        }
    } else if (s->rejected_record && s->rejected_record_generation==t.generation) {
        if (bo_route_resolve(ownership,t,BO_SUPPRESS)!=BO_NONE) return BA_INVALID;
        return BA_CONSUMED_RELEASE;
    }
    return BA_ROUTE_GESTURE;
}

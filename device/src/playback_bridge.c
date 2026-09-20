#include "playback_bridge.h"

static int address_ok(uint32_t p) { return p && !(p & 3u); }

static PbResult fail(PlaybackBridge *b, PbResult why) {
    b->fault = (uint8_t)why;
    b->zero_window = b->cycle_window = 0;
    b->transport = PB_UNKNOWN;
    return why;
}

static int event(PlaybackBridge *b) {
    if (b->serial == UINT32_MAX) {
        fail(b, PB_DESYNC); /* Never wrap and accidentally revalidate a permit. */
        return 0;
    }
    ++b->serial;
    return 1;
}

static int base_guard(const PbStock *s, uint32_t expected,unsigned length) {
    return s->current_block == expected && s->ui_mode == 1 &&
        s->selector == 0 && length>=1 && length<=64 &&
        s->header[0] == length && s->cached_length == length &&
        s->target_11 == 0xff && !s->reset_request && s->reset_target == 0xff &&
        !s->retrigger_request && s->retrigger_target == 0xff && !s->phase_target &&
        !s->switch_request && s->switch_target == 0xff &&
        !s->rebuild_request && s->rebuild_target == 0xff;
}

static PbResult window(const PlaybackBridge *b, const PbStock *s) {
    if (b->transport == PB_STOPPED && !b->step_active && !s->emit && !s->paused &&
        s->ui_transport == 0 && s->selected_step == -1 &&
        s->release_deadline == INT32_MAX) return PB_PUBLISH_STOPPED;
    if (b->transport == PB_RUNNING && b->step_active && b->zero_window &&
        s->emit == 1 && !s->paused && s->ui_transport == 2 &&
        s->selected_step == 0) return PB_PUBLISH_ZERO;
    return PB_WAIT;
}

PbResult pb_init(PlaybackBridge *b, uint32_t installed_block,unsigned length) {
    if (!b || !address_ok(installed_block) || length<1 || length>64) return PB_BAD_ARGUMENT;
    b->current_block = installed_block;
    b->length=(uint8_t)length;b->cycle_window=0;
    b->serial = 0;
    b->transport = PB_UNKNOWN;
    b->have_step = b->last_step = b->step_active = b->zero_window = b->fault = 0;
    return PB_OK;
}

PbResult pb_transport_after(PlaybackBridge *b, unsigned action, const PbStock *s) {
    if (!b) return PB_BAD_ARGUMENT;
    if (!event(b)) return PB_DESYNC;
    b->zero_window = b->cycle_window = 0;
    if (!s) return fail(b, PB_BAD_ARGUMENT);
    if (b->step_active) return fail(b, PB_INTERLEAVED);
    if (action > 4) return fail(b, PB_BAD_ARGUMENT);
    if (action == 4) return fail(b, PB_REPHASE_UNSUPPORTED);
    if (!base_guard(s, b->current_block,b->length)) return fail(b, PB_GUARD_FAILED);
    if (action == 0) {
        if (s->emit || s->paused || s->selected_step != -1 ||
            s->release_deadline != INT32_MAX || s->note_release)
            return fail(b, PB_GUARD_FAILED);
        b->transport = PB_STOPPED;
        b->have_step = b->fault = 0;
        return PB_OK;
    }
    if (b->fault) return (PbResult)b->fault;
    if (action == 2) {
        if (b->transport != PB_RUNNING || s->emit || s->paused != 1 || s->note_release)
            return fail(b, PB_GUARD_FAILED);
        b->transport = PB_PAUSED;
        return PB_OK;
    }
    if (s->emit != 1 || s->paused) return fail(b, PB_GUARD_FAILED);
    if (action == 1) {
        if (b->transport == PB_UNKNOWN) return fail(b, PB_DESYNC);
        b->have_step = 0;
    } else if (b->transport != PB_PAUSED) {
        return fail(b, PB_DESYNC);
    }
    b->transport = PB_RUNNING;
    return PB_OK;
}

PbResult pb_step_before(PlaybackBridge *b,unsigned step,const PbStock *s) {
    return pb_step_before_cycle(b,step,s,0);
}
PbResult pb_step_before_cycle(PlaybackBridge *b, unsigned step, const PbStock *s,unsigned native_wrap) {
    if (!b) return PB_BAD_ARGUMENT;
    if (!event(b)) return PB_DESYNC;
    if (!s) return fail(b, PB_BAD_ARGUMENT);
    if (b->step_active) return fail(b, PB_INTERLEAVED);
    b->zero_window = b->cycle_window = 0;
    if (step >= b->length || native_wrap>1) return fail(b, PB_DESYNC);
    if (b->fault) return (PbResult)b->fault;
    if (!base_guard(s, b->current_block,b->length) || b->transport != PB_RUNNING ||
        s->emit != 1 || s->paused || s->ui_transport != 2 ||
        s->selected_step != (int32_t)step) return fail(b, PB_GUARD_FAILED);
    if (!b->have_step) {
        if (step != 0) return fail(b, PB_DESYNC);
        b->zero_window = 1;
    } else if (step != b->last_step) {
        if (step != (b->last_step+1u==b->length?0u:b->last_step+1u)) return fail(b, PB_DESYNC);
        b->zero_window = b->cycle_window = step == 0;
    } else if(b->length==1 && step==0 && native_wrap) {
        b->zero_window=b->cycle_window=1;
    }
    /* A repeated call for step0 is a real stock possibility. Only a fresh
     * pre-clear native wrap can authorize another completed one-step cycle. */
    b->last_step = (uint8_t)step;
    b->have_step = b->step_active = 1;
    return PB_OK;
}

PbResult pb_step_after(PlaybackBridge *b) {
    if (!b) return PB_BAD_ARGUMENT;
    if (!event(b)) return PB_DESYNC;
    if (!b->step_active) return fail(b, PB_INTERLEAVED);
    b->step_active = b->zero_window = b->cycle_window = 0;
    return b->fault ? (PbResult)b->fault : PB_OK;
}

PbResult pb_time_division_step(PlaybackBridge *b,unsigned step,const PbStock *s) {
    if(!b)return PB_BAD_ARGUMENT;
    if(!event(b))return PB_DESYNC;
    if(!s)return fail(b,PB_BAD_ARGUMENT);
    if(b->step_active)return fail(b,PB_INTERLEAVED);
    b->zero_window=b->cycle_window=0;
    if(b->fault)return (PbResult)b->fault;
    if(step>=b->length || !base_guard(s,b->current_block,b->length) || b->transport!=PB_RUNNING ||
       s->emit!=1 || s->paused || s->ui_transport!=2 || s->selected_step!=(int32_t)step)
        return fail(b,PB_GUARD_FAILED);
    b->last_step=(uint8_t)step;b->have_step=b->step_active=1;
    return PB_OK;
}

PbResult pb_external_change(PlaybackBridge *b) {
    if (!b) return PB_BAD_ARGUMENT;
    if (!event(b)) return PB_DESYNC;
    return fail(b, PB_DESYNC);
}

PbResult pb_offer(PlaybackBridge *b, const PbStock *s, uint32_t next_block,
                  const uint8_t next_header[8], PbPermit *permit) {
    PbResult kind;
    unsigned i;
    if (!b) return PB_BAD_ARGUMENT;
    if (!event(b)) return PB_DESYNC;
    if (b->serial == UINT32_MAX) return fail(b, PB_DESYNC); /* Reserve one ack event. */
    if (!s || !next_header || !permit || !address_ok(next_block) ||
        next_block == b->current_block) return PB_BAD_ARGUMENT;
    if (b->fault) return (PbResult)b->fault;
    if (!base_guard(s, b->current_block,b->length)) return PB_GUARD_FAILED;
    kind = window(b, s);
    if (kind == PB_WAIT) return kind;
    if(next_header[0]<1 || next_header[0]>64)return PB_HEADER_CHANGED;
    for (i = 1; i < 8; ++i) if (s->header[i] != next_header[i]) return PB_HEADER_CHANGED;
    if (s->note_release) return PB_NEED_STOCK_CLEANUP;
    if (s->note_suppressed || s->note_retain) return PB_GUARD_FAILED;
    permit->serial = b->serial;
    permit->previous_block = b->current_block;
    permit->next_block = next_block;
    permit->kind = (uint8_t)kind;permit->previous_length=b->length;
    for (i = 0; i < 8; ++i) permit->header[i] = next_header[i];
    return kind;
}

PbResult pb_observe_swap(PlaybackBridge *b, const PbStock *s, const PbPermit *p) {
    unsigned i;
    uint32_t offered_serial;
    if (!b) return PB_BAD_ARGUMENT;
    offered_serial = b->serial;
    if (!event(b)) return PB_DESYNC;
    if (!s || !p) return PB_BAD_ARGUMENT;
    if (b->fault) return (PbResult)b->fault;
    if (p->serial != offered_serial || p->previous_block != b->current_block ||
        p->previous_length!=b->length ||
        !address_ok(p->next_block) || p->next_block == p->previous_block ||
        (p->kind != PB_PUBLISH_ZERO && p->kind != PB_PUBLISH_STOPPED) ||
        window(b, s) != (PbResult)p->kind) return PB_STALE_PERMIT;
    if (!base_guard(s, p->next_block,p->header[0]) || s->note_release ||
        s->note_suppressed || s->note_retain) return PB_GUARD_FAILED;
    for (i = 0; i < 8; ++i) if (s->header[i] != p->header[i]) return PB_HEADER_CHANGED;
    b->current_block = p->next_block;b->length=p->header[0];
    b->zero_window = b->cycle_window = 0; /* One musical transaction per playback event. */
    return PB_OK;
}

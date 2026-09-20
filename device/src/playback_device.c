#include "playback_device.h"
#include "fault_trace11.h"

#define C UINT32_C(0x20002bec)
#define S UINT32_C(0x20002c4c)
#define N UINT32_C(0x20004ed4)
#define U UINT32_C(0x20002ddc)

static int span(uint32_t a) { return a >= 0x20005eb0u && !(a & 3u) && a <= UINT32_MAX - STOCK_SEQUENCE_BYTES; }
static int stock_block(uint32_t a) {
    return a == 0x200027e4u || (a >= 0x20002e94u && a <= 0x20004accu &&
                              (a - 0x20002e94u) % STOCK_SEQUENCE_BYTES == 0);
}
static int owned(const PlaybackDevice *d, uint32_t a) { return a == d->blocks[0] || a == d->blocks[1]; }
static PdResult fault_at11(PlaybackDevice *d,PdResult why,unsigned site) {
    pd_trace_fault11(d,site,why,d->bridge.fault);
    d->phase = PD_FAULT;
    (void)pb_external_change(&d->bridge);
    return why;
}
#define fault(d,why) fault_at11((d),(why),99)
static int tick(PlaybackDevice *d) {
    if (d->epoch == UINT32_MAX) { (void)fault(d, PD_INTERLEAVE); return 0; }
    ++d->epoch;
    return 1;
}
static int authority(uint32_t p) { return (p & PD_REQUIRED) == PD_REQUIRED; }
static int thread(void) { return !pd_hw_ipsr() && !(pd_hw_control() & 1u); }
static unsigned division(unsigned x) {
    /* TBB08011e14:04,16,07,09,0b,0e,10,12. Its first target11e1c
     * returns480; second11e40 returns240 (instruction order is different). */
    switch (x) {
    case 0: return 480; case 1: return 240; case 2: return 120; case 3: return 60;
    case 4: return 320; case 5: return 160; case 6: return 80; case 7: return 40;
    default: return 0;
    }
}
static int read_view(const PlaybackDevice *d, PdView *v) {
    PbStock *b = &v->stock;
    unsigned i;
    if (pd_hw_read32(0x20001098u) != C || pd_hw_read32(0x20001150u) != S ||
        pd_hw_read32(0x20001124u) != U || pd_hw_read32(C+0x4c) != S ||
        pd_hw_read32(C+0x50) != N || pd_hw_read32(N) != S) return 0;
    b->current_block = pd_hw_read32(S);
    if (!stock_block(b->current_block) && !owned(d,b->current_block)) return 0;
    v->pending = pd_hw_read32(S+4);
    v->division = pd_hw_read8(C+0x48);
    v->reload_shadow = pd_hw_read32(0x200054c8u);
    b->selected_step = (int32_t)pd_hw_read32(C+0x38);
    b->release_deadline = (int32_t)pd_hw_read32(C+0x44);
    b->phase_target = (int32_t)pd_hw_read32(C+0x58);
    b->ui_mode = pd_hw_read8(U+0x0f); b->ui_transport = pd_hw_read8(U+0x10);
    b->emit = pd_hw_read8(C+4); b->paused = pd_hw_read8(C+5);
    b->selector = pd_hw_read8(C+0x0c); b->cached_length = pd_hw_read8(C+0x10);
    b->target_11 = pd_hw_read8(C+0x11); b->reset_request = pd_hw_read8(C+0x12);
    b->reset_target = pd_hw_read8(C+0x13);
    b->retrigger_request = pd_hw_read8(C+0x54); b->retrigger_target = pd_hw_read8(C+0x55);
    b->switch_request = pd_hw_read8(C+0x56); b->switch_target = pd_hw_read8(C+0x57);
    b->rebuild_request = pd_hw_read8(C+0x5d); b->rebuild_target = pd_hw_read8(C+0x5e);
    b->note_release = pd_hw_read8(N+0x25);
    b->note_suppressed = pd_hw_read8(N+0x27); b->note_retain = pd_hw_read8(N+0x28);
    for (i=0;i<8;++i) b->header[i] = pd_hw_read8(b->current_block+0x400+i);
    return 1;
}
static int idle(const PdView *v) {
    const PbStock *b = &v->stock;
    return b->ui_mode == 1 && !b->ui_transport && !b->emit && !b->paused &&
        !b->selector && b->selected_step == -1 && b->release_deadline == INT32_MAX &&
        b->target_11 == 0xff && !b->reset_request && b->reset_target == 0xff &&
        !b->retrigger_request && b->retrigger_target == 0xff && !b->phase_target &&
        !b->switch_request && b->switch_target == 0xff &&
        !b->rebuild_request && b->rebuild_target == 0xff &&
        !b->note_suppressed && !b->note_retain && division(v->division) &&
        b->header[0] >= 1 && b->header[0] <= 64;
}
static int timing(const PdView *v) {
    return v->stock.cached_length == v->stock.header[0] &&
        v->reload_shadow == division(v->division) * v->stock.header[0] - 1u;
}
static int same_context(const PdView *a, const PdView *b) {
    unsigned i;
    if (a->stock.current_block != b->stock.current_block || a->pending != b->pending ||
        a->division != b->division || a->stock.cached_length != b->stock.cached_length ||
        a->reload_shadow != b->reload_shadow) return 0;
    for(i=0;i<8;++i) if(a->stock.header[i]!=b->stock.header[i]) return 0;
    return 1;
}
static int saved_header(const PlaybackDevice *d) {
    unsigned i;
    for(i=0;i<8;++i) if(pd_hw_read8(d->saved_current+0x400+i)!=d->saved_header[i]) return 0;
    return 1;
}
static PdResult lock(uint32_t *mask) {
    if (!thread()) return PD_CONTEXT;
    *mask = pd_hw_irq_save();
    /* Stock cleanup/service call trees are not proved safe with PRIMASK=1.
     * Enter/leave/event must arrive with interrupts enabled, then restore that
     * exact state around stock calls. No blanket mask encloses NoteOff I/O. */
    if (*mask) { pd_hw_irq_restore(*mask); return PD_CONTEXT; }
    return PD_OK;
}
int pd_init(PlaybackDevice *d, uint32_t a, uint32_t b) {
    unsigned i;
    if (!d || !span(a) || !span(b) || (a < b ? b-a : a-b) < STOCK_SEQUENCE_BYTES) return 0;
    /* Object is new/unowned storage, see lifetime contract in header. */
    for(i=0;i<sizeof(*d);++i) ((uint8_t *)d)[i]=0;
    d->blocks[0]=a; d->blocks[1]=b;
    return pb_init(&d->bridge,a,1)==PB_OK;
}

PdResult pd_enter(PlaybackDevice *d, const GenSnapshot *g, uint32_t proofs) {
    PdView before, after;
    uint32_t mask, epoch;
    unsigned i;
    PdResult r;
    if (!d || !g || d->phase!=PD_OFF) return PD_BAD_ARGUMENT;
    if (!authority(proofs)) return PD_NO_AUTHORITY;
    if ((r=lock(&mask))!=PD_OK) return r;
    if (!tick(d) || !read_view(d,&before) || !stock_block(before.stock.current_block) ||
        !idle(&before) || !timing(&before)) {
        pd_hw_irq_restore(mask); return PD_STOCK_GUARD;
    }
    epoch=d->epoch;
    d->phase=PD_TRANSITION; /* Any callback during preparation revokes entry. */
    pd_hw_irq_restore(mask);
    if (!stock_sequence_encode(pd_hw_block(d->blocks[0]),g,before.stock.header)) {
        if ((r=lock(&mask))!=PD_OK) return fault(d,r);
        if (d->epoch==epoch && d->phase==PD_TRANSITION) { d->phase=PD_OFF; r=PD_CODEC; }
        else r=fault(d,PD_INTERLEAVE);
        pd_hw_irq_restore(mask); return r;
    }
    /* A real release retains the saved N pitches/channel. Never reinitialize N
     * or fabricate N25=0, and never clear N27/N28 by hand. */
    pd_hw_cleanup();
    if ((r=lock(&mask))!=PD_OK) return fault(d,r);
    if (d->epoch!=epoch || d->phase!=PD_TRANSITION || !read_view(d,&after) || !idle(&after) ||
        after.stock.note_release || !same_context(&before,&after)) {
        r=fault(d,PD_INTERLEAVE); pd_hw_irq_restore(mask); return r;
    }
    d->saved_current=before.stock.current_block; d->saved_pending=before.pending;
    d->saved_length=before.stock.cached_length; d->saved_division=before.division;
    d->saved_reload=before.reload_shadow;
    for(i=0;i<8;++i) d->saved_header[i]=before.stock.header[i];
    d->phase=PD_TRANSITION; d->queued=0;
    pd_hw_store_current(d->blocks[0]);
    pd_hw_irq_restore(mask);
    /* The original length transition owns both C10 and its timer limit. */
    pd_hw_service();
    pd_hw_stop();
    if ((r=lock(&mask))!=PD_OK) return fault(d,r);
    if (d->epoch!=epoch || !read_view(d,&after) || !idle(&after) || !timing(&after) ||
        after.stock.current_block!=d->blocks[0] || after.stock.header[0]!=g->params.length ||
        after.pending!=d->saved_pending || after.division!=d->saved_division ||
        after.stock.note_release || !saved_header(d)) {
        r=fault(d,PD_PUBLICATION_FAULT);
    } else {
        (void)pb_init(&d->bridge,d->blocks[0],after.stock.header[0]);
        if (pb_transport_after(&d->bridge,0,&after.stock)!=PB_OK) r=fault(d,PD_PUBLICATION_FAULT);
        else { d->phase=PD_ACTIVE; r=PD_PUBLISHED; }
    }
    pd_hw_irq_restore(mask);
    return r;
}

PdResult pd_queue(PlaybackDevice *d, const GenSnapshot *g, uint32_t proofs) {
    PdView v, again;
    uint32_t mask, epoch, target;
    PdResult r;
    if (!d || !g || d->phase!=PD_ACTIVE) return PD_BAD_ARGUMENT;
    if (!authority(proofs)) return PD_NO_AUTHORITY;
    if ((r=lock(&mask))!=PD_OK) return r;
    /* Do not replace a queued buffer: it may be published by a PRE callback
     * while encoding is outside the mask. Caller must first consume it. */
    if (d->queued || d->step_frame || d->transport_frame || !read_view(d,&v) ||
        v.stock.current_block!=d->bridge.current_block || !owned(d,v.stock.current_block) ||
        v.stock.header[0]!=d->bridge.length || g->params.length!=d->bridge.length || !tick(d)) {
        pd_hw_irq_restore(mask); return PD_STOCK_GUARD;
    }
    epoch=d->epoch; target=v.stock.current_block==d->blocks[0]?d->blocks[1]:d->blocks[0];
    pd_hw_irq_restore(mask);
    if (!stock_sequence_encode(pd_hw_block(target),g,v.stock.header)) return PD_CODEC;
    if ((r=lock(&mask))!=PD_OK) return r;
    if (d->epoch!=epoch || !read_view(d,&again) || !same_context(&v,&again)) r=PD_INTERLEAVE;
    else { d->queued=target==d->blocks[0]?1:2; r=PD_OK; }
    pd_hw_irq_restore(mask);
    return r;
}

static PdResult publish(PlaybackDevice *d) {
    PdView v;
    PbPermit permit;
    PbResult p;
    PdResult r;
    uint32_t mask, epoch, target;
    uint8_t header[8];
    unsigned i;
    if (!d->queued) return PD_WAIT;
    target=d->blocks[d->queued-1];
    if ((r=lock(&mask))!=PD_OK) return fault(d,r);
    epoch=d->epoch;
    if (!read_view(d,&v) || v.pending!=d->saved_pending || v.division!=d->saved_division) {
        r=fault(d,PD_STOCK_GUARD); pd_hw_irq_restore(mask); return r;
    }
    for(i=0;i<8;++i) header[i]=pd_hw_read8(target+0x400+i);
    p=pb_offer(&d->bridge,&v.stock,target,header,&permit);
    if (p==PB_NEED_STOCK_CLEANUP) {
        pd_hw_irq_restore(mask);
        pd_hw_cleanup();
        if ((r=lock(&mask))!=PD_OK) return fault(d,r);
        if (d->epoch!=epoch || !read_view(d,&v) || v.pending!=d->saved_pending ||
            v.division!=d->saved_division) {
            r=fault(d,PD_INTERLEAVE); pd_hw_irq_restore(mask); return r;
        }
        p=pb_offer(&d->bridge,&v.stock,target,header,&permit);
    }
    if (p!=PB_PUBLISH_STOPPED && p!=PB_PUBLISH_ZERO) {
        pd_hw_irq_restore(mask); return p==PB_WAIT?PD_WAIT:PD_STOCK_GUARD;
    }
    pd_hw_store_current(target);
    if (!read_view(d,&v) || pb_observe_swap(&d->bridge,&v.stock,&permit)!=PB_OK)
        r=fault(d,PD_PUBLICATION_FAULT);
    else { d->queued=0; r=PD_PUBLISHED; }
    pd_hw_irq_restore(mask);
    return r;
}
PdResult pd_publish_stopped(PlaybackDevice *d, uint32_t proofs) {
    if (!d || d->phase!=PD_ACTIVE) return PD_BAD_ARGUMENT;
    if (!authority(proofs)) return PD_NO_AUTHORITY;
    if (d->step_frame || d->transport_frame) return fault(d,PD_INTERLEAVE);
    return publish(d);
}

PdResult pd_event(PlaybackDevice *d, unsigned e, uint32_t receiver,
                  unsigned value, uint32_t proofs) {
    PdView v;
    PbResult p=PB_OK;
    PdResult r;
    uint32_t mask;
    if (!d || e>PD_STEP_POST) return PD_BAD_ARGUMENT;
    if (d->phase==PD_OFF) return PD_PASS;
    if (!authority(proofs)) return fault(d,PD_NO_AUTHORITY);
    if ((r=lock(&mask))!=PD_OK) return fault(d,r);
    if (!tick(d) || d->phase!=PD_ACTIVE ||
        receiver!=((e==PD_STEP_PRE||e==PD_STEP_POST)?N:C)) {
        r=fault(d,PD_INTERLEAVE); pd_hw_irq_restore(mask); return r;
    }
    if (e==PD_TRANSPORT_PRE) {
        if (d->transport_frame || d->step_frame) r=fault(d,PD_INTERLEAVE);
        else { d->transport_frame=1; r=PD_OK; }
    } else if (e==PD_TRANSPORT_POST) {
        if (!d->transport_frame || d->step_frame || !read_view(d,&v)) r=fault(d,PD_INTERLEAVE);
        else {
            d->transport_frame=0;
            p=pb_transport_after(&d->bridge,value,&v.stock);
            r=p==PB_OK?PD_OK:fault(d,PD_STOCK_GUARD);
        }
    } else if (e==PD_STEP_PRE) {
        if (d->transport_frame || d->step_frame || !read_view(d,&v)) r=fault(d,PD_INTERLEAVE);
        else {
            d->step_frame=1;
            p=pb_step_before(&d->bridge,value,&v.stock);
            r=p==PB_OK?PD_OK:fault(d,PD_STOCK_GUARD);
        }
    } else {
        if (!d->step_frame) r=fault(d,PD_INTERLEAVE);
        else {
            d->step_frame=0; p=pb_step_after(&d->bridge);
            r=p==PB_OK?PD_OK:fault(d,PD_STOCK_GUARD);
        }
    }
    pd_hw_irq_restore(mask);
    if (r==PD_OK && e==PD_STEP_PRE && d->queued) return publish(d);
    return r;
}

PdResult pd_leave(PlaybackDevice *d, uint32_t proofs) {
    PdView before, after;
    uint32_t mask, epoch;
    PdResult r;
    if (!d || d->phase!=PD_ACTIVE) return PD_BAD_ARGUMENT;
    if (!authority(proofs)) return PD_NO_AUTHORITY;
    if ((r=lock(&mask))!=PD_OK) return r;
    if (d->step_frame || d->transport_frame || !read_view(d,&before) || !idle(&before) ||
        !owned(d,before.stock.current_block) || before.pending!=d->saved_pending ||
        before.division!=d->saved_division || !saved_header(d) || !tick(d)) {
        pd_hw_irq_restore(mask); return PD_STOCK_GUARD;
    }
    epoch=d->epoch;
    pd_hw_irq_restore(mask);
    pd_hw_cleanup();
    if ((r=lock(&mask))!=PD_OK) return r;
    if(d->epoch!=epoch || !read_view(d,&after) || !idle(&after) || after.stock.note_release ||
       !same_context(&before,&after) || !saved_header(d)) {
        pd_hw_irq_restore(mask); return PD_INTERLEAVE;
    }
    d->phase=PD_TRANSITION;
    pd_hw_store_current(d->saved_current);
    pd_hw_irq_restore(mask);
    pd_hw_service();
    if((r=lock(&mask))!=PD_OK) return fault(d,r);
    if(d->epoch!=epoch || !read_view(d,&after) || !idle(&after) || !timing(&after) ||
       after.stock.current_block!=d->saved_current || after.pending!=d->saved_pending ||
       after.stock.cached_length!=d->saved_length || after.division!=d->saved_division ||
       after.reload_shadow!=d->saved_reload || !saved_header(d)) r=fault(d,PD_PUBLICATION_FAULT);
    else { d->phase=PD_OFF; d->queued=0; r=PD_OK; }
    pd_hw_irq_restore(mask);
    return r;
}

static int masked_authority(uint32_t p) {
    const uint32_t need=PD_RESERVED_BLOCKS|PD_SERVICE_FRAME_ACCOUNTED|PD_VALID_NATIVE_HEADER;
    return (p&need)==need;
}
static int ram_pointer(uint32_t a, unsigned bytes) {
    /* This is a bounds/corruption guard, not proof of allocator ownership.
     * Objects belong to the already completed original stock startup. */
    return !(a&3u) && a>=0x20000000u && a<=0x2000c000u-bytes;
}
/* Return the failing condition without writing native state. The same predicate
 * controls admission and diagnostics, so a diagnostic never relaxes a guard. */
static unsigned fast_guard_reason(const PlaybackDevice *d, PdView *v) {
    uint32_t midi;
    if (!read_view(d,v)) return 41;
    if (!idle(v)) return 42;
    if (!timing(v)) return 43;
    if (v->stock.note_release) return 51;
    if (pd_hw_read32(0x200010d8u)!=0x20004f18u) return 52;
    if (pd_hw_read8(0x2000509eu)!=pd_hw_read8(0x2000509fu)) return 53;
    if (!(pd_hw_read8(C+0x24)|pd_hw_read8(C+0x25))) return 54;
    if (pd_hw_read8(0x2000521du) && pd_hw_read8(0x200010a8u)) return 55;
    if (pd_hw_read8(0x20005228u)&2u) return 56;
    midi=pd_hw_read32(0x20000214u);
    if(!ram_pointer(midi,160)) return 57;
    if(!ram_pointer(pd_hw_read32(0x20001178u),264)) return 58;
    /* 1878c..18790 stores literal187cc=40000800 into200054bc. */
    uint32_t timer=pd_hw_read32(0x200054bcu);
    if(timer!=0x40000800u) return 59;
    if(pd_hw_timer_word(0)&1u) return 60;
    if(pd_hw_timer_word(0x24)) return 61;
    return 0;
}
static int fast_guard(const PlaybackDevice *d, PdView *v) {
    return fast_guard_reason(d,v)==0;
}
/* ENTER may prepare an encoded block for a stopped nonzero CNT. This does
 * not grant publication: the actual native Stop and complete guard recheck
 * remain mandatory under the final transaction lock. All earlier fast guards
 * (including CEN==0) still have to pass. */
static int entry_prepare_guard(const PlaybackDevice *d,PdView *v) {
    unsigned reason=fast_guard_reason(d,v);
    return reason==0 || reason==61;
}
PdResult pd_enter_masked(PlaybackDevice *d, const GenSnapshot *g, uint32_t proofs) {
    PdView before, after;
    uint32_t mask, epoch;
    PdResult r;
    unsigned i;
    if(!d || !g || d->phase!=PD_OFF) return PD_BAD_ARGUMENT;
    if(!masked_authority(proofs)) return PD_NO_AUTHORITY;
    if((r=lock(&mask))!=PD_OK) return r;
    if(!fast_guard(d,&before) || !stock_block(before.stock.current_block) || !tick(d)) {
        pd_hw_irq_restore(mask); return PD_STOCK_GUARD;
    }
    epoch=d->epoch;d->phase=PD_TRANSITION;
    pd_hw_irq_restore(mask);
    int encoded=stock_sequence_encode(pd_hw_block(d->blocks[0]),g,before.stock.header);
    if((r=lock(&mask))!=PD_OK) return fault(d,r);
    if(d->epoch!=epoch || d->phase!=PD_TRANSITION || !fast_guard(d,&after) ||
       !same_context(&before,&after)) {
        r=fault(d,PD_INTERLEAVE);pd_hw_irq_restore(mask);return r;
    }
    if(!encoded){d->phase=PD_OFF;pd_hw_irq_restore(mask);return PD_CODEC;}
    d->saved_current=before.stock.current_block;d->saved_pending=before.pending;
    d->saved_length=before.stock.cached_length;d->saved_division=before.division;
    d->saved_reload=before.reload_shadow;d->queued=0;
    for(i=0;i<8;++i)d->saved_header[i]=before.stock.header[i];
    /* Under these guards14258 is its two-branch leaf early return; Stop's
     * only serial output is a nonblocking one-byte FC enqueue. No allocation
     * or hardware-ready wait is reachable in this documented subset. */
    pd_hw_cleanup();
    pd_hw_stop();
    if(!fast_guard(d,&after) || !same_context(&before,&after)) r=fault(d,PD_INTERLEAVE);
    else {
        pd_hw_store_current(d->blocks[0]);
        pd_hw_service();
        if(!fast_guard(d,&after) || after.stock.current_block!=d->blocks[0] ||
           after.pending!=d->saved_pending || after.division!=d->saved_division ||
           after.stock.header[0]!=g->params.length || !saved_header(d)) r=fault(d,PD_PUBLICATION_FAULT);
        else {
            (void)pb_init(&d->bridge,d->blocks[0],after.stock.header[0]);
            if(pb_transport_after(&d->bridge,0,&after.stock)!=PB_OK) r=fault(d,PD_PUBLICATION_FAULT);
            else {d->phase=PD_ACTIVE;r=PD_PUBLISHED;}
        }
    }
    pd_hw_irq_restore(mask);return r;
}
PdResult pd_leave_masked(PlaybackDevice *d, uint32_t proofs) {
    PdView before,after;
    uint32_t mask;
    PdResult r;
    if(!d || d->phase!=PD_ACTIVE) return PD_BAD_ARGUMENT;
    if(!masked_authority(proofs)) return PD_NO_AUTHORITY;
    if((r=lock(&mask))!=PD_OK) return r;
    if(d->step_frame || d->transport_frame || !fast_guard(d,&before) ||
       !owned(d,before.stock.current_block) || before.pending!=d->saved_pending ||
       before.division!=d->saved_division || !saved_header(d) || !tick(d)) {
        pd_hw_irq_restore(mask);return PD_STOCK_GUARD;
    }
    d->phase=PD_TRANSITION;
    pd_hw_cleanup();
    pd_hw_store_current(d->saved_current);
    pd_hw_service();
    if(!fast_guard(d,&after) || after.stock.current_block!=d->saved_current ||
       after.pending!=d->saved_pending || after.division!=d->saved_division ||
       after.stock.cached_length!=d->saved_length || after.reload_shadow!=d->saved_reload ||
       !saved_header(d)) r=fault(d,PD_PUBLICATION_FAULT);
    else {d->phase=PD_OFF;d->queued=0;r=PD_OK;}
    pd_hw_irq_restore(mask);return r;
}

/* Prepared integration: only the encoder runs outside the critical section. */
static void bytes_copy(void *to, const void *from, unsigned count) {
    uint8_t *a=to; const uint8_t *b=from;
    while(count--) *a++=*b++;
}
static int bytes_equal(const void *a0,const void *b0,unsigned count) {
    const uint8_t *a=a0,*b=b0;
    while(count--) if(*a++!=*b++) return 0;
    return 1;
}
static PdResult already_locked(void) {
    uint32_t mask;
    if(!thread()) return PD_CONTEXT;
    mask=pd_hw_irq_save();
    if(mask!=1u) { pd_hw_irq_restore(mask); return PD_CONTEXT; }
    return PD_OK;
}
static void revoke_prepared(PlaybackDevice *d,PdPrepared *p) {
    p->ready=0;
    if(p->serial==d->prepared_serial && d->prepared_serial!=UINT32_MAX)
        ++d->prepared_serial;
}
static int active_context(const PlaybackDevice *d,const PdView *v) {
    return d->phase==PD_ACTIVE && owned(d,v->stock.current_block) &&
        v->stock.current_block==d->bridge.current_block &&
        v->pending==d->saved_pending && v->division==d->saved_division &&
        v->stock.header[0]==d->bridge.length && timing(v) && saved_header(d);
}
/* Preserve the existing RAM layout. division_rebase is an owned byte:
 * bit0 means the first emitted step after a witnessed timer rewrite; bits1..7
 * encode target+1 (1..64), or0 when native C55 has been observed consumed.
 * A live target persists independently of bit0 because Swing can emit the
 * preceding step before native12f48 consumes C55. Only an actual applied
 * witness creates a target; no snapshot can mint or extend this authority. */
enum { PD_DIV_FIRST_STEP=1, PD_DIV_TARGET_SHIFT=1, PD_DIV_TARGET_MASK=254 };
static unsigned division_target(const PlaybackDevice *d) {
    unsigned tag=(d->division_rebase&PD_DIV_TARGET_MASK)>>PD_DIV_TARGET_SHIFT;
    return tag?tag-1u:255u;
}
static int division_has_target(const PlaybackDevice *d) {
    return !!(d->division_rebase&PD_DIV_TARGET_MASK);
}
static int division_first_step(const PlaybackDevice *d) {
    return !!(d->division_rebase&PD_DIV_FIRST_STEP);
}
static void division_target_consumed(PlaybackDevice *d) {
    d->division_rebase&=PD_DIV_FIRST_STEP;
}
static void division_step_observed(PlaybackDevice *d) {
    d->division_rebase&=PD_DIV_TARGET_MASK;
}
/* The11ee8 request licenses its pending C54/C55 state; an actual12cd2 timer
 * rewrite licenses exactly its computed C55 until native consumes it. Swing
 * may delay that consumption and make129cc repeat the same native retime.
 * Normalize only this private policy view, never native C54/C55. */
static int division_policy_view(PlaybackDevice *d,PdView *v) {
    if(d->division_requested) {
        if(d->requested_division>7 || pd_hw_read8(C+0x49)!=d->requested_division ||
           v->stock.retrigger_request>1 ||
           (v->stock.retrigger_target!=255 && v->stock.retrigger_target>=d->bridge.length))return 0;
        v->stock.retrigger_request=0;v->stock.retrigger_target=255;
        return 1;
    }
    if(!division_has_target(d))return 1;
    unsigned target=division_target(d);
    if(target>=d->bridge.length || v->stock.retrigger_request ||
       v->division!=d->saved_division || d->requested_division!=d->saved_division ||
       pd_hw_read8(C+0x49)!=d->saved_division)return 0;
    if(v->stock.retrigger_target==255)division_target_consumed(d);
    else if(v->stock.retrigger_target!=target)return 0;
    v->stock.retrigger_target=255;
    return 1;
}
static void division_revoke(PlaybackDevice *d,PdPrepared *p) {
    if(p)p->ready=0;
    ++d->prepared_serial;
    ++d->bridge.serial;d->bridge.zero_window=0;
}
PdResult pd_time_division_request_locked(PlaybackDevice *d,PdPrepared *p,
                                       uint32_t receiver,unsigned value) {
    PdView v;PdResult r;
    if(!d)return PD_BAD_ARGUMENT;
    if(d->phase==PD_OFF)return PD_PASS;
    if((r=already_locked())!=PD_OK)return r;
    if(d->phase!=PD_ACTIVE)return fault_at11(d,PD_STOCK_GUARD,10);
    if(receiver!=C || value>7)return fault_at11(d,PD_STOCK_GUARD,11);
    if(d->step_frame || d->transport_frame || d->bridge.step_active || d->native_step_witness)return fault_at11(d,PD_STOCK_GUARD,12);
    if(d->bridge.fault)return fault_at11(d,PD_STOCK_GUARD,13);
    if(d->prepared_serial==UINT32_MAX || d->bridge.serial==UINT32_MAX)return fault_at11(d,PD_STOCK_GUARD,14);
    if(!read_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,15);
    if(!active_context(d,&v))return fault_at11(d,PD_STOCK_GUARD,16);
    if(!division_policy_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,18);
    if(!d->division_requested&&(v.stock.retrigger_request||v.stock.retrigger_target!=255))return fault_at11(d,PD_STOCK_GUARD,17);
    division_revoke(d,p);d->division_requested=1;d->requested_division=(uint8_t)value;
    return PD_OK;
}
PdResult pd_time_division_applied_locked(PlaybackDevice *d,PdPrepared *p,
                                       uint32_t receiver,unsigned value,
                                       uint32_t phase,uint32_t reload) {
    PdView v;PdResult r;unsigned ticks=division(value);
    if(!d)return PD_BAD_ARGUMENT;
    if(d->phase==PD_OFF)return PD_PASS;
    if((r=already_locked())!=PD_OK)return r;
    if(d->phase!=PD_ACTIVE || receiver!=C)return fault_at11(d,PD_STOCK_GUARD,20);
    if(!d->division_requested&&!division_has_target(d))return fault_at11(d,PD_STOCK_GUARD,21);
    if(d->requested_division!=value)return fault_at11(d,PD_STOCK_GUARD,22);
    if(!ticks || phase>=ticks*d->bridge.length)return fault_at11(d,PD_STOCK_GUARD,23);
    if(reload!=ticks*d->bridge.length-1u)return fault_at11(d,PD_STOCK_GUARD,24);
    if(d->step_frame || d->transport_frame || d->bridge.step_active || d->native_step_witness)return fault_at11(d,PD_STOCK_GUARD,25);
    if(d->bridge.fault)return fault_at11(d,PD_STOCK_GUARD,26);
    if(d->prepared_serial==UINT32_MAX || d->bridge.serial==UINT32_MAX)return fault_at11(d,PD_STOCK_GUARD,27);
    if(!read_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,28);
    if(!owned(d,v.stock.current_block) || v.stock.current_block!=d->bridge.current_block)return fault_at11(d,PD_STOCK_GUARD,29);
    if(v.pending!=d->saved_pending)return fault_at11(d,PD_STOCK_GUARD,30);
    if(v.division!=value || pd_hw_read8(C+0x49)!=value)return fault_at11(d,PD_STOCK_GUARD,31);
    if(v.stock.header[0]!=d->bridge.length || !timing(&v))return fault_at11(d,PD_STOCK_GUARD,32);
    if(v.stock.retrigger_request>1)return fault_at11(d,PD_STOCK_GUARD,33);
    if(v.stock.retrigger_target>=d->bridge.length)return fault_at11(d,PD_STOCK_GUARD,37);
    if(!d->division_requested && (v.stock.retrigger_request || d->saved_division!=value ||
       v.stock.retrigger_target!=division_target(d)))return fault_at11(d,PD_STOCK_GUARD,38);
    if(!saved_header(d))return fault_at11(d,PD_STOCK_GUARD,34);
    if(v.stock.selector || v.stock.ui_mode!=1)return fault_at11(d,PD_STOCK_GUARD,35);
    if(pd_hw_timer_word(0x2c)!=reload)return fault_at11(d,PD_STOCK_GUARD,36);
    /* Native12c6c tests one BC reading, then12cae rereads BC to calculate
     * phase. An IRQ between them may produce a legitimate nonaligned phase;
     * the witnessed request, bounded phase, native timing and ARR still hold. */
    division_revoke(d,p);
    /* Physical Time Div persists when leaving GEN; restore the native
     * block's length at this new division, not the old entry-time reload. */
    d->saved_division=(uint8_t)value;d->saved_reload=ticks*d->saved_length-1u;
    /* A replacement11ee8 can change C49 while the previous C55 is live.
     * Native12a90 may then apply that value through the old target before
     * new-divisor alignment consumes C54. Retain the actual pending request
     * until a later authenticated native apply observes C54==0. */
    d->division_requested=v.stock.retrigger_request;
    d->division_rebase=(uint8_t)(PD_DIV_FIRST_STEP |
        ((phase/ticks+1u)<<PD_DIV_TARGET_SHIFT));
    return PD_OK;
}
static PdResult prepare(PlaybackDevice *d,PdPrepared *p,const GenState *g,unsigned kind) {
    PdView again;
    uint32_t mask,serial,target;
    PdResult r;
    int encoded=1;
    if(!d || !p || !g) return PD_BAD_ARGUMENT;
    if((r=lock(&mask))!=PD_OK) return r;
    p->ready=0;
    if(d->prepared_serial>=UINT32_MAX-1u) {
        pd_hw_irq_restore(mask);return PD_INTERLEAVE;
    }
    serial=++d->prepared_serial;
    if(d->transport_frame || d->step_frame || d->native_step_witness || d->queued ||
       !read_view(d,&p->native) ||
       (kind==PD_PREPARE_ENTER ? (d->phase!=PD_OFF || g->pending_count ||
          !stock_block(p->native.stock.current_block) || !entry_prepare_guard(d,&p->native)) :
          !active_context(d,&p->native)) ||
       ((kind==PD_PREPARE_PUBLISH && !g->pending_count) ||
        (kind==PD_PREPARE_CYCLE && d->completed_cycles==UINT32_MAX))) {
        pd_hw_irq_restore(mask);return PD_STOCK_GUARD;
    }
    target=kind==PD_PREPARE_ENTER?d->blocks[0]:kind==PD_PREPARE_LEAVE?d->saved_current:
        p->native.stock.current_block==d->blocks[0]?d->blocks[1]:d->blocks[0];
    p->kind=(uint8_t)kind;p->target=target;p->serial=serial;
    p->cycle_epoch=d->completed_cycles+(kind==PD_PREPARE_CYCLE);
    bytes_copy(&p->core,g,sizeof(*g));
    pd_hw_irq_restore(mask);
    bytes_copy(&p->proposed,&p->core,sizeof(p->proposed));
    if(kind==PD_PREPARE_PUBLISH || kind==PD_PREPARE_CYCLE)
        gen_boundary_ex(&p->proposed,kind==PD_PREPARE_CYCLE);
    if(kind!=PD_PREPARE_LEAVE)
        encoded=stock_sequence_encode(pd_hw_block(target),&p->proposed.current,p->native.stock.header);
    if((r=lock(&mask))!=PD_OK) return r;
    if(d->prepared_serial!=serial || !bytes_equal(&p->core,g,sizeof(*g)) ||
       !read_view(d,&again) || !same_context(&p->native,&again) ||
       d->transport_frame || d->step_frame || d->native_step_witness ||
       (kind==PD_PREPARE_CYCLE && p->cycle_epoch!=d->completed_cycles+1u) ||
       (kind==PD_PREPARE_ENTER?d->phase!=PD_OFF:!active_context(d,&again))) r=PD_STALE_PREPARED;
    else if(!encoded) r=PD_CODEC;
    else {p->ready=1;r=PD_OK;}
    pd_hw_irq_restore(mask);return r;
}
PdResult pd_prepare_enter(PlaybackDevice *d,PdPrepared *p,const GenState *g) {
    return prepare(d,p,g,PD_PREPARE_ENTER);
}
PdResult pd_prepare_publish(PlaybackDevice *d,PdPrepared *p,const GenState *g) {
    return prepare(d,p,g,PD_PREPARE_PUBLISH);
}
PdResult pd_prepare_leave(PlaybackDevice *d,PdPrepared *p,const GenState *g) {
    return prepare(d,p,g,PD_PREPARE_LEAVE);
}
PdResult pd_prepare_cycle(PlaybackDevice *d,PdPrepared *p,const GenState *g) {
    return prepare(d,p,g,PD_PREPARE_CYCLE);
}
/* Narrow mono release through the ORIGINAL14258 call tree. Exact callback,
 * output object and peripheral pins exclude lazy allocation/unknown BLX.
 * See inspect_playback_release.py:14258 ->1b6c4 ->1b384 ->1ad20/1ae56.
 * The tracker finalizer has at most128 iterations. DIN enqueues at most3 bytes;
 * USB at most1 word; neither waits for IRQ progress. */
static int ring_room(uint32_t object,unsigned capacity,unsigned need) {
    uint32_t head=pd_hw_read32(object),tail=pd_hw_read32(object+4);
    if(head>=capacity || tail>=capacity) return 0;
    unsigned free=tail>head?tail-head-1u:capacity-head+tail-1u;
    return free>=need;
}
static int release_profile(const PdView *v) {
    const uint32_t voice=0x20001eb8u,x=0x20001e04u,r=0x20001d60u,k=0x200013fcu;
    uint32_t midi=pd_hw_read32(0x20000214u),din=pd_hw_read32(0x20001178u);
    if(v->stock.ui_mode!=1 || v->stock.selector ||
       pd_hw_read8(U+0xc)==1 || pd_hw_read8(U+0x14)!=0 ||
       pd_hw_read32(voice)!=x || pd_hw_read32(x)!=r || pd_hw_read8(x+0x34)!=0 ||
       pd_hw_read32(r)!=0x20001d5cu || pd_hw_read32(r+4)!=0x08014b7du ||
       pd_hw_read32(r+8)!=0x20001d5cu || pd_hw_read32(r+0xc)!=0x08014bb1u ||
       pd_hw_read32(r+0x10)!=k || pd_hw_read32(r+0x14)!=0x08014b6du ||
       pd_hw_read32(0x20001d5cu)!=0x200000c0u ||
       !ram_pointer(midi,160) || din!=0x200050c4u ||
       pd_hw_read32(midi)>=36 || pd_hw_read32(midi+4)>=36 ||
       pd_hw_read32(din)>=256 || pd_hw_read32(din+4)>=256 ||
       pd_hw_read32(0x200055e4u)!=0x40013800u ||
       pd_hw_read32(0x2000111cu)!=0x20002cf8u ||
       pd_hw_read32(0x2000115cu)!=0x20000668u || pd_hw_read8(0x2000066fu) ||
       pd_hw_read32(0x20002d00u)!=0x40010800u ||
       (pd_hw_read8(0x20002d04u)|(unsigned)pd_hw_read8(0x20002d05u)<<8)!=2u ||
       pd_hw_read32(k+0x948)!=r+0x20 || pd_hw_read8(k+0x952)!=1 ||
       pd_hw_read8(0x20005285u)) return 0;
    unsigned low=pd_hw_read8(k+0x950),high=pd_hw_read8(k+0x951);
    int32_t special=(int32_t)pd_hw_read32(0x200000c8u);
    /* d378 constructor48..85 and d878 octave setter use width37. d77c
     * includes high, giving maximum pixel39; d26e accepts indices0..40. */
    return low<=high && high<=127 && high-low<=37 && special>=-1 && special<=127;
}
PdResult pd_entry_profile_locked(PlaybackDevice *d) {
    PdView v;PdResult result;
    if(!d)return PD_BAD_ARGUMENT;
    if((result=already_locked())!=PD_OK)return result;
    return read_view(d,&v)&&release_profile(&v)?PD_OK:PD_STOCK_GUARD;
}
unsigned pd_diagnostic_locked(PlaybackDevice *d) {
    PdView v;
    unsigned reason;
    if(!d || already_locked()!=PD_OK)return 40;
    if(!read_view(d,&v))return 41;
    if(!idle(&v))return 42;
    if(!timing(&v))return 43;
    reason=fast_guard_reason(d,&v);
    if(reason)return reason;
    if(!release_profile(&v))return 45;
    return 0;
}
static int release_guard(const PdView *v) {
    unsigned pitch=pd_hw_read8(N+5);
    return release_profile(v) && v->stock.note_release && !v->stock.note_suppressed &&
        !v->stock.note_retain && pitch<=127 && pd_hw_read8(N+6)==255 &&
        !(pd_hw_read8(0x20001d80u+pitch)&3u) && !(pd_hw_read8(0x20001e00u)&3u);
}
static int commit_matches(const PdPrepared *p,const GenState *g) {
    return bytes_equal(g,&p->proposed,sizeof(*g));
}
PdResult pd_apply_prepared_locked(PlaybackDevice *d,PdPrepared *p,GenState *g,
                                  PdCommitFn cb,void *ctx) {
    PdView before,after;
    PbPermit permit;
    PbResult offered;
    PdResult r;
    uint8_t header[8];
    unsigned i,kind;
    if(!d || !p || !g || !cb) return PD_BAD_ARGUMENT;
    if((r=already_locked())!=PD_OK) return r;
    if(p->ready!=1 || p->serial!=d->prepared_serial ||
       !bytes_equal(&p->core,g,sizeof(*g))) {
        revoke_prepared(d,p);return PD_STALE_PREPARED;
    }
    kind=p->kind;
    if(kind<PD_PREPARE_ENTER || kind>PD_PREPARE_CYCLE) {
        revoke_prepared(d,p);return PD_BAD_ARGUMENT;
    }
    if(!read_view(d,&before) || !same_context(&p->native,&before)) {
        revoke_prepared(d,p);return PD_STALE_PREPARED;
    }
    if(kind==PD_PREPARE_PUBLISH || kind==PD_PREPARE_CYCLE) {
        if(kind==PD_PREPARE_CYCLE) {
            if(p->cycle_epoch<d->completed_cycles){revoke_prepared(d,p);return PD_STALE_PREPARED;}
            if(!d->bridge.cycle_window || p->cycle_epoch!=d->completed_cycles)return PD_WAIT;
        }
        if(!division_policy_view(d,&before))return PD_STOCK_GUARD;
        if(d->division_requested || division_has_target(d))return PD_WAIT;
        if(!active_context(d,&before) || d->transport_frame ||
           !owned(d,p->target) || p->target==before.stock.current_block)
            return PD_STOCK_GUARD;
        for(i=0;i<8;++i) header[i]=pd_hw_read8(p->target+0x400+i);
        if(header[0]!=p->proposed.current.params.length)return PD_STOCK_GUARD;
        offered=pb_offer(&d->bridge,&before.stock,p->target,header,&permit);
        if(offered==PB_WAIT) return PD_WAIT;
        if(offered!=PB_PUBLISH_STOPPED && offered!=PB_PUBLISH_ZERO &&
           offered!=PB_NEED_STOCK_CLEANUP)return PD_STOCK_GUARD;
        unsigned next_length=header[0],old_length=d->bridge.length;
        unsigned ticks=division(before.division);
        uint32_t next_reload=ticks*next_length-1u;
        /* The actual old-cycle step0 frame has already consumed its old
         * length. Cache/ARR/shadow change with a bounded CEN hold; CNT/BC stay intact. */
        if(next_length!=old_length) {
            if(!ticks || pd_hw_read32(0x200054bcu)!=0x40000800u ||
               pd_hw_timer_word(0x2c)!=before.reload_shadow ||
               pd_hw_timer_word(0)!=before.stock.emit || pd_hw_timer_word(8)!=0x17u ||
               (pd_hw_timer_word(0x20)&0x1555u))return PD_STOCK_GUARD;
            if(before.stock.emit) {
                if(!d->step_frame || d->native_step!=0 || d->native_phase>=ticks ||
                   pd_hw_timer_word(0x24)>=ticks)return PD_WAIT;
            }else if(pd_hw_timer_word(0x24) || (pd_hw_timer_word(0)&1u))return PD_STOCK_GUARD;
        }
        if(offered==PB_NEED_STOCK_CLEANUP) {
            if(!release_guard(&before)) return PD_STOCK_GUARD;
            if(!ring_room(pd_hw_read32(0x20000214u),36,1) || !ring_room(0x200050c4u,256,3))
                return PD_WAIT; /* Pressure is observable pending, never fabricated NoteOff. */
            pd_hw_cleanup();
            if(!read_view(d,&after) || !same_context(&before,&after) ||
               after.stock.note_release || after.stock.note_suppressed || after.stock.note_retain)
                return fault_at11(d,PD_INTERLEAVE,60);
            offered=pb_offer(&d->bridge,&after.stock,p->target,header,&permit);
        }
        if(offered!=PB_PUBLISH_STOPPED && offered!=PB_PUBLISH_ZERO) return PD_STOCK_GUARD;
        if(next_length!=old_length) {
            /* The hardware counter advances even with PRIMASK1. The MMIO
             * transaction proves its native profile, briefly holds CEN0,
             * rereads the now stable phase and restores exact CR1 on WAIT
             * and success; a partial failure retains ownership. */
            r=pd_hw_publish_length(p->target,next_length,next_reload,ticks,before.stock.emit);
            if(r==PD_PUBLICATION_FAULT) {revoke_prepared(d,p);return fault_at11(d,r,70);}
            if(r!=PD_PUBLISHED)return r;
        }else pd_hw_store_current(p->target);
        revoke_prepared(d,p);
        if(!read_view(d,&after) || !timing(&after) ||
           after.pending!=before.pending || after.division!=before.division ||
           pd_hw_timer_word(0x2c)!=next_reload ||
           pb_observe_swap(&d->bridge,&after.stock,&permit)!=PB_OK)
            return fault_at11(d,PD_PUBLICATION_FAULT,61);
        cb(ctx,kind);
        if(!commit_matches(p,g) || g->current.params.length!=d->bridge.length || pd_hw_irq_save()!=1u) return fault_at11(d,PD_COMMIT_FAULT,62);
        return PD_PUBLISHED;
    }
    if(kind==PD_PREPARE_LEAVE && (!active_context(d,&before) || p->target!=d->saved_current))
        return PD_STOCK_GUARD;
    /* A stopped native retime can leave CNT=BC%(D*accepted_length), although CEN is0.
     * A real Stop at this main boundary normalizes that known state. All
     * earlier fast guards must pass; no hand-written CNT/N clearing. */
    if(kind==PD_PREPARE_LEAVE &&
       (division_first_step(d)||division_has_target(d)) && !d->division_requested &&
       d->bridge.transport==PB_STOPPED && !d->step_frame && !d->transport_frame &&
       fast_guard_reason(d,&before)==61) {
        if(!release_profile(&before))return PD_STOCK_GUARD;
        if(!ring_room(pd_hw_read32(0x20000214u),36,1) || !ring_room(0x200050c4u,256,1))
            return PD_WAIT;
        after=before;pd_hw_stop();
        if(!fast_guard(d,&before) || !same_context(&after,&before) || !active_context(d,&before))
            return fault_at11(d,PD_INTERLEAVE,63);
        d->division_rebase=0;
    }
    if(d->step_frame || d->transport_frame ||
       (kind==PD_PREPARE_ENTER?!entry_prepare_guard(d,&before):!fast_guard(d,&before)))
        return PD_STOCK_GUARD;
    if(kind==PD_PREPARE_ENTER) {
        if(d->phase!=PD_OFF || p->target!=d->blocks[0] || !stock_block(before.stock.current_block))
            return PD_STOCK_GUARD;
        /* Validate the prepared block's retained seven bytes before native I/O. */
        if(pd_hw_read8(p->target+0x400)!=p->core.current.params.length) return PD_STOCK_GUARD;
        for(i=1;i<8;++i) if(pd_hw_read8(p->target+0x400+i)!=before.stock.header[i])
            return PD_STOCK_GUARD;
        /* Stop emits one FC into each real output ring. Do not consume the
         * ticket, mutate native transport, or save ownership state until the
         * exact bounded native call profile and both capacities are proven.
         * With N25==0 the cleanup paths return without a NoteOff. */
        if(!release_profile(&before)) return PD_STOCK_GUARD;
        if(!ring_room(pd_hw_read32(0x20000214u),36,1) || !ring_room(0x200050c4u,256,1))
            return PD_WAIT;
        pd_hw_cleanup();pd_hw_stop();
        if(!fast_guard(d,&after) || !same_context(&before,&after))
            return fault_at11(d,PD_INTERLEAVE,64);
        /* A post-Stop fault above retains PD_FAULT ownership although STR and
         * D0 have not happened: native Stop already had observable effects. */
        revoke_prepared(d,p);
        d->saved_current=before.stock.current_block;d->saved_pending=before.pending;
        d->saved_length=before.stock.cached_length;d->saved_division=before.division;
        d->saved_reload=before.reload_shadow;d->queued=0;
        for(i=0;i<8;++i)d->saved_header[i]=before.stock.header[i];
        d->phase=PD_TRANSITION;
        pd_hw_store_current(p->target);pd_hw_service();
        if(!fast_guard(d,&after) || after.stock.current_block!=p->target ||
           after.pending!=d->saved_pending || after.division!=d->saved_division ||
           after.stock.header[0]!=p->core.current.params.length || !saved_header(d)) return fault_at11(d,PD_PUBLICATION_FAULT,65);
        (void)pb_init(&d->bridge,p->target,after.stock.header[0]);
        if(pb_transport_after(&d->bridge,0,&after.stock)!=PB_OK) return fault_at11(d,PD_PUBLICATION_FAULT,66);
        d->phase=PD_ACTIVE;d->division_requested=0;d->division_rebase=0;
        d->completed_cycles=0;d->native_step_witness=d->native_wrap=0;
        cb(ctx,kind);
        if(!bytes_equal(&p->core,g,sizeof(*g)) || pd_hw_irq_save()!=1u) return fault_at11(d,PD_COMMIT_FAULT,67);
        return PD_PUBLISHED;
    }
    if(!active_context(d,&before) || p->target!=d->saved_current) return PD_STOCK_GUARD;
    revoke_prepared(d,p);d->phase=PD_TRANSITION;
    pd_hw_cleanup();pd_hw_store_current(p->target);pd_hw_service();
    if(!fast_guard(d,&after) || after.stock.current_block!=d->saved_current ||
       after.pending!=d->saved_pending || after.division!=d->saved_division ||
       after.stock.cached_length!=d->saved_length || after.reload_shadow!=d->saved_reload ||
       !saved_header(d)) return fault_at11(d,PD_PUBLICATION_FAULT,68);
    d->phase=PD_OFF;d->queued=0;d->division_requested=0;d->division_rebase=0;
    d->native_step_witness=d->native_wrap=0;
    cb(ctx,kind);
    if(pd_hw_irq_save()!=1u) return fault_at11(d,PD_COMMIT_FAULT,69);
    return PD_OK;
}
PdResult pd_event_prepared_locked(PlaybackDevice *d,PdPrepared *ticket,GenState *g,
                                  unsigned e,uint32_t receiver,unsigned value,
                                  PdCommitFn cb,void *ctx) {
    PdView v;
    PdResult r;
    PbResult p;
    if(!d || e>PD_STEP_POST) return PD_BAD_ARGUMENT;
    if((r=already_locked())!=PD_OK) return r;
    if(d->phase==PD_OFF) {
        /* A real transport operation supersedes stopped entry preparation. */
        if(e==PD_TRANSPORT_PRE && d->prepared_serial!=UINT32_MAX) ++d->prepared_serial;
        return PD_PASS;
    }
    if(receiver!=((e==PD_STEP_PRE||e==PD_STEP_POST)?N:C)) return fault_at11(d,PD_INTERLEAVE,40);
    /* Mandatory POST clears its frame even after a PRE publication fault. */
    if(d->phase==PD_FAULT) {
        if(e==PD_TRANSPORT_POST) d->transport_frame=0;
        if(e==PD_STEP_POST) {d->step_frame=0;(void)pb_step_after(&d->bridge);}
        return PD_PUBLICATION_FAULT;
    }
    if(d->phase!=PD_ACTIVE) return fault_at11(d,PD_INTERLEAVE,41);
    if(e==PD_TRANSPORT_PRE) {
        if(d->transport_frame || d->step_frame || d->native_step_witness) return fault_at11(d,PD_INTERLEAVE,42);
        d->transport_frame=1;return PD_OK;
    }
    if(e==PD_TRANSPORT_POST) {
        if(!d->transport_frame || d->step_frame) return fault_at11(d,PD_INTERLEAVE,43);
        d->transport_frame=0;
        if(!read_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,44);
        if(!active_context(d,&v))return fault_at11(d,PD_STOCK_GUARD,45);
        if(!division_policy_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,46);
        if(pb_transport_after(&d->bridge,value,&v.stock)!=PB_OK)return fault_at11(d,PD_STOCK_GUARD,47);
        if(value==0||value==1)division_step_observed(d);
        return PD_OK;
    }
    if(e==PD_STEP_POST) {
        d->native_step_witness=d->native_wrap=0;
        if(!d->step_frame) return fault_at11(d,PD_INTERLEAVE,48);
        d->step_frame=0;
        return pb_step_after(&d->bridge)==PB_OK?PD_OK:fault_at11(d,PD_STOCK_GUARD,49);
    }
    if(d->step_frame || d->transport_frame)return fault_at11(d,PD_STOCK_GUARD,50);
    if(!read_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,51);
    if(!active_context(d,&v))return fault_at11(d,PD_STOCK_GUARD,52);
    if(!division_policy_view(d,&v))return fault_at11(d,PD_STOCK_GUARD,53);
    if(!d->native_step_witness || d->native_step!=value)return fault_at11(d,PD_STOCK_GUARD,58);
    unsigned wrapped=d->native_wrap;d->native_step_witness=0;
    d->step_frame=1;
    p=division_first_step(d)?pb_time_division_step(&d->bridge,value,&v.stock):
                         pb_step_before_cycle(&d->bridge,value,&v.stock,wrapped);
    if(p!=PB_OK) return fault_at11(d,PD_STOCK_GUARD,
        (v.stock.retrigger_request||v.stock.retrigger_target!=255)?56:
        division_first_step(d)?55:p==PB_DESYNC?54:57);
    division_step_observed(d);
    if(d->bridge.cycle_window) {
        if(d->completed_cycles==UINT32_MAX)return fault_at11(d,PD_INTERLEAVE,59);
        ++d->completed_cycles;
    }
    if(ticket && ticket->ready && (ticket->kind==PD_PREPARE_PUBLISH || ticket->kind==PD_PREPARE_CYCLE))
        return pd_apply_prepared_locked(d,ticket,g,cb,ctx);
    return PD_OK;
}

PdResult pd_step_witness_locked(PlaybackDevice *d,uint32_t receiver,unsigned step,
                               unsigned native_wrap,uint32_t native_phase) {
    PdView v;PdResult r;
    if(!d)return PD_BAD_ARGUMENT;
    if(d->phase==PD_OFF)return PD_PASS;
    if((r=already_locked())!=PD_OK)return r;
    if(d->phase!=PD_ACTIVE || receiver!=C || d->step_frame || d->transport_frame ||
       d->native_step_witness || native_wrap>1 || pd_hw_read8(C+6)!=native_wrap ||
       !read_view(d,&v) || !active_context(d,&v) || step>=d->bridge.length ||
       v.stock.selected_step!=(int32_t)step ||
       native_phase>=division(v.division)*d->bridge.length)
        return fault_at11(d,PD_STOCK_GUARD,58);
    d->native_step_witness=1;d->native_step=(uint8_t)step;
    d->native_wrap=(uint8_t)native_wrap;d->native_phase=native_phase;
    return PD_OK;
}

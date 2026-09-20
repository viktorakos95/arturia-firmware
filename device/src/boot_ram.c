#include "boot_ram.h"
#include <stddef.h>

static uint32_t guard(unsigned i) { return 0xb007cafeu ^ (0x9e3779b9u * (i+1u)); }
static void increment(uint32_t *value) { if (*value!=UINT32_MAX) ++*value; }
static int fail(BootRam *s, uint32_t fault) {
    if(s) { s->faults|=fault; s->phase=BOOT_FAILED; }
    return 0;
}
int boot_ram_layout(uint32_t count,uint32_t margin,BootLayout *out) {
    uint32_t begin=BOOT_RAM_START+((sizeof(BootRam)+7u)&~7u)+BOOT_GUARD_BYTES;
    if(!out || !count || (margin&7u) || margin<32u ||
       count>BOOT_INITIAL_SP-begin-BOOT_GUARD_BYTES-7u) return 0;
    uint32_t end=((begin+count+7u)&~7u)+BOOT_GUARD_BYTES;
    if(margin>BOOT_INITIAL_SP-end) return 0;
    out->payload_address=begin; out->payload_capacity=count;
    out->reserved_end=end; out->stack_margin=margin;
    return 1;
}
static int valid_layout(const BootLayout *l) {
    BootLayout expected;
    return l && boot_ram_layout(l->payload_capacity,l->stack_margin,&expected) &&
        expected.payload_address==l->payload_address && expected.reserved_end==l->reserved_end;
}
static int valid_view(BootRamView v) {
    return v.state && v.guard_low && v.payload && v.guard_high;
}
static int context_ok(BootObservation o) {
    /* MSP only, privileged thread setup; probes may execute in handler mode.
     * SPSEL/privilege changes are not silently accepted as the same model. */
    /* Handler mode is privileged and uses MSP regardless of the interrupted
     * thread's CONTROL.nPRIV/SPSEL values. Thread admission remains strict. */
    return (o.ipsr || !(o.control&3u)) && !(o.msp&3u) && o.msp<=BOOT_INITIAL_SP;
}
static uint32_t effective_break(const BootLayout *l,BootObservation o) {
    return o.break_value ? o.break_value : l->reserved_end;
}
static void sample(BootRam *s,uint32_t msp,uint32_t brk) {
    if(!s->minimum_sampled_msp || msp<s->minimum_sampled_msp) s->minimum_sampled_msp=msp;
    if(brk>s->maximum_sampled_break) s->maximum_sampled_break=brk;
    increment(&s->probes);
}
int boot_ram_initialize(BootRamView v,const BootLayout *l,BootObservation o,BootPayloadInit init) {
    if(!valid_view(v)) return 0;
    if(!valid_layout(l)) return fail(v.state,BOOT_F_LAYOUT);
    /* Header is owned and reset-zeroed even if admission fails. */
    const unsigned char *bytes=(const unsigned char *)v.state;
    for(unsigned i=0;i<sizeof(*v.state);++i)
        if(bytes[i]) return fail(v.state,BOOT_F_STATE);
    if(o.break_value || o.heap_origin || o.free_head) return fail(v.state,BOOT_F_STATE);
    if(o.ipsr || !context_ok(o)) return fail(v.state,BOOT_F_CONTEXT);
    if(o.msp<l->reserved_end || o.msp-l->reserved_end<l->stack_margin)
        return fail(v.state,BOOT_F_SPACE);
    for(unsigned i=0;i<BOOT_GUARD_WORDS;++i)
        if(v.guard_low[i] || v.guard_high[i]) return fail(v.state,BOOT_F_STATE);
    v.state->payload_address=l->payload_address; v.state->payload_capacity=l->payload_capacity;
    v.state->reserved_end=l->reserved_end; v.state->stack_margin=l->stack_margin;
    v.state->phase=BOOT_INITIALIZING;
    for(unsigned i=0;i<BOOT_GUARD_WORDS;++i) v.guard_low[i]=v.guard_high[i]=guard(i);
    unsigned char *payload=(unsigned char *)v.payload;
    for(uint32_t i=0;i<l->payload_capacity;++i) payload[i]=0;
    sample(v.state,o.msp,l->reserved_end);
    if(!init || !init(v.payload,l->payload_capacity)) return fail(v.state,BOOT_F_INIT);
    /* A forbidden allocation or probe failure during init cannot be erased by
     * a later successful callback return. */
    if(v.state->phase!=BOOT_INITIALIZING || v.state->faults) return fail(v.state,BOOT_F_INIT);
    for(unsigned i=0;i<BOOT_GUARD_WORDS;++i)
        if(v.guard_low[i]!=guard(i) || v.guard_high[i]!=guard(i)) return fail(v.state,BOOT_F_CANARY);
    v.state->phase=BOOT_READY;
    v.state->published=1;
    return 1;
}
int boot_ram_probe(BootRamView v,const BootLayout *l,BootObservation o) {
    if(!valid_view(v) || v.state->phase!=BOOT_READY) return 0;
    if(!valid_layout(l) || v.state->payload_address!=l->payload_address ||
       v.state->payload_capacity!=l->payload_capacity || v.state->reserved_end!=l->reserved_end ||
       v.state->stack_margin!=l->stack_margin) return fail(v.state,BOOT_F_LAYOUT);
    if(!context_ok(o)) return fail(v.state,BOOT_F_CONTEXT);
    uint32_t brk=effective_break(l,o);
    sample(v.state,o.msp,brk);
    if(brk<l->reserved_end) return fail(v.state,BOOT_F_BREAK);
    if(brk>o.msp || o.msp-brk<l->stack_margin) return fail(v.state,BOOT_F_SPACE);
    for(unsigned i=0;i<BOOT_GUARD_WORDS;++i)
        if(v.guard_low[i]!=guard(i) || v.guard_high[i]!=guard(i)) return fail(v.state,BOOT_F_CANARY);
    return 1;
}
void *boot_ram_payload(BootRamView v,const BootLayout *l,BootObservation o) {
    if(!valid_view(v) || v.state->published!=1) return NULL;
    (void)boot_ram_probe(v,l,o);
    return v.payload;
}
int boot_ram_growth(BootRam *s,const BootLayout *l,BootObservation o,int32_t delta,
                    uint32_t *old_break,uint32_t *new_break) {
    uint32_t fault=0,old=0,next=0;
    if(!s || !old_break || !new_break) return 0;
    if(!valid_layout(l)) fault=BOOT_F_LAYOUT;
    else if(!context_ok(o)) fault=BOOT_F_CONTEXT;
    else {
        old=effective_break(l,o);
        if(old<l->reserved_end) fault=BOOT_F_BREAK;
        else if(s->phase==BOOT_INITIALIZING && delta) fault=BOOT_F_INIT_ALLOC;
        else if(delta>=0) {
            uint32_t add=(uint32_t)delta;
            if(add>UINT32_MAX-old) fault=BOOT_F_BREAK;
            else next=old+add;
        } else {
            uint32_t subtract=0u-(uint32_t)delta;
            if(subtract>old-l->reserved_end) fault=BOOT_F_BREAK;
            else next=old-subtract;
        }
        if(!fault && (next>o.msp || o.msp-next<l->stack_margin)) fault=BOOT_F_SPACE;
    }
    if(fault) { increment(&s->denied_growth); return fail(s,fault); }
    sample(s,o.msp,next); *old_break=old; *new_break=next;
    return 1;
}

#include "generator_device_runtime.h"
#include "fault_trace11.h"
#include "generator_rate12.h"
#include <stddef.h>
#include "boot_ram.h"
#include "control_input_policy.h"
#if !defined(__thumb__)
#error "Device target requires ARM Thumb"
#endif

typedef struct { GeneratorDeviceRuntime runtime; FaultTrace11 trace; } RuntimeStorageV4;
__attribute__((section(".boot_ram.payload"),used,aligned(8)))
static RuntimeStorageV4 runtime_storage;
_Static_assert(offsetof(RuntimeStorageV4,trace)==sizeof(GeneratorDeviceRuntime),"Trace must follow complete V4 runtime");
_Static_assert(sizeof(RuntimeStorageV4)==sizeof(GeneratorDeviceRuntime)+sizeof(FaultTrace11),"Diagnostic payload must have no hidden tail");
_Static_assert(sizeof(GeneratorDeviceRuntime)==10240,"Reviewed V4 runtime geometry");
_Static_assert(sizeof(RuntimeStorageV4)==10248,"Reviewed V4 diagnostic payload");
extern unsigned char __boot_ram_end[];
static uint32_t r32(uint32_t a) {return *(const volatile uint32_t *)(uintptr_t)a;}
static uint8_t r8(uint32_t a) {return *(const volatile uint8_t *)(uintptr_t)a;}
static void w8(uint32_t a,unsigned v) {*(volatile uint8_t *)(uintptr_t)a=(uint8_t)v;}

int boot_payload_init(void *p,uint32_t capacity) {
    if(p!=&runtime_storage.runtime||capacity<sizeof(runtime_storage))return 0;
    runtime_storage.trace=(FaultTrace11){0};
    return gd_init(p,sizeof(runtime_storage.runtime));
}

/* Neither this classification nor the trace grants permission to continue. */
static unsigned existing_fault11(const GeneratorDeviceRuntime *s) {
    unsigned f=s->base.faults;
    if(f&DR_BOOT_FAULT)return 70;
    if(f&DR_INPUT_FAULT)return 71;
    if(f&DR_CONTEXT_FAULT)return 72;
    if(f&DR_LED_FAULT)return 73;
    if(f&GD_BAD_PROFILE)return 74;
    if(f&GD_NATIVE_FAULT)return 75;
    if(f&GD_TRANSPORT_OVERFLOW)return 76;
    if(f&GD_PREPARE_FAULT)return 77;
    if(f&GD_PUBLICATION_FAULT)return 78;
    if(f)return 79;
    f=s->buttons.faults;
    for(unsigned bit=0;bit<5;++bit)if(f&(1u<<bit))return 80+bit;
    return f?85:0;
}
static void trace_locked11(GeneratorDeviceRuntime *s,unsigned site,unsigned result,unsigned bridge) {
    if(s!=&runtime_storage.runtime||runtime_storage.trace.code)return;
    unsigned old=existing_fault11(s),code=old?old:site;
    if(!code)return;
    FaultTrace11 *t=&runtime_storage.trace;
    t->site=(uint8_t)site;t->result=(uint8_t)result;t->bridge=(uint8_t)bridge;
    t->prior_flags=s->base.faults|(s->buttons.faults<<16);
    /* Publish code last; readers and writers share the same preserved mask. */
    t->code=(uint8_t)(code<=99?code:99);
}
void pd_trace_fault11(PlaybackDevice *d,unsigned site,unsigned result,unsigned bridge) {
    uint32_t mask=pd_hw_irq_save();
    if(d==&runtime_storage.runtime.device)
        trace_locked11(&runtime_storage.runtime,site,result,bridge);
    pd_hw_irq_restore(mask);
}
void gd_trace_fault11(GeneratorDeviceRuntime *s,unsigned code,unsigned result) {
    uint32_t mask=pd_hw_irq_save();
    if(s==&runtime_storage.runtime)trace_locked11(s,code,result,s->device.bridge.fault);
    pd_hw_irq_restore(mask);
}
unsigned gd_trace_code11(GeneratorDeviceRuntime *s) {
    uint32_t mask=pd_hw_irq_save();unsigned code=0;
    if(s==&runtime_storage.runtime){
        trace_locked11(s,s->device.phase==PD_FAULT?90:0,0,s->device.bridge.fault);
        code=runtime_storage.trace.code;
    }
    pd_hw_irq_restore(mask);return code;
}

GeneratorDeviceRuntime *ks37_generator_device_state(void) {
    GeneratorDeviceRuntime *s=boot_payload_get();
    if(s) {
        const volatile BootRam *b=boot_ram_status_get();
        if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
        if(b->phase!=BOOT_READY||b->faults){gd_trace_fault11(s,70,b->faults);s->base.faults|=DR_BOOT_FAULT;}
    }
    return s;
}
uint32_t gd_hw_cpuid(void) {return r32(0xe000ed00u);}
uint32_t gd_hw_tick(void) {return r32(0x20000294u);}
unsigned gd_hw_mode_detent(void) {
    /* Main1577c already called physical Mode poll5a20 before157b4. Read the
     * stable native debounce sample, not the deferred native6d ack. */
    return r8(0x20000549u);
}
int gd_hw_context(GeneratorDeviceRuntime *s,GenGestureContext *out) {
    Ks37NativeDeviceRead n;
    if(!ks37_native_read_device(&n))return 0;
    ks37_native_gesture_context(out,&n.snapshot);
    /* Source birth and continuity are checked separately. Empty queues here
     * close only deferred native messages and clock actions at this instant;
     * completed running-status parsers are deliberately NOT required to be0. */
    unsigned queues=r32(0x200010e0u)==0x200023f8u&&
        r32(0x20002538u)==r32(0x2000253cu)&&
        r32(0x20002728u)==r32(0x2000272cu)&&
        r32(0x200010d8u)==0x20004f18u&&r8(0x2000509eu)==r8(0x2000509fu);
    if(!(out->known&GG_K_PENDING)) {
        out->known|=GG_K_PENDING;
        out->pending=(uint8_t)(!queues||!s->inputs.cold_used||s->inputs.lost_sources||
            pd_entry_profile_locked(&s->device)!=PD_OK);
    }
    return 1;
}
int gd_hw_finish_chord(void) {
    /* This is only the exact D0 effect of17d98, not the stock release handler:
     * page-toggle/Chord-capture release must not run after a consumed combo. */
    if(r32(0x20001124u)!=0x20002ddcu||r8(0x20002de9u)>1||
       r8(0x200010d2u)||r8(0x20005285u))return 0;
    w8(0x20002de9u,0);__asm__ volatile("dmb":::"memory");
    return r8(0x20002de9u)==0;
}
BdaBinding *ks37_button_device_binding(void) {
    GeneratorDeviceRuntime *s=ks37_generator_device_state();
    return s?&s->button_binding:0;
}
int ks37_input_device_bind(InputDeviceBinding *out) {
    GeneratorDeviceRuntime *s=ks37_generator_device_state();
    if(!s)return 0;
    *out=(InputDeviceBinding){&s->inputs,&s->base.input,s,gd_input_effects,
        (uint32_t)(uintptr_t)__boot_ram_end,r32(0x200052a8u),
        !(s->base.faults&(DR_BOOT_FAULT|GD_BAD_PROFILE))};
    return 1;
}
unsigned ks37_control_input_entry(uint32_t object,uint32_t id,uint32_t before,uint32_t after) {
    uint32_t mask=pd_hw_irq_save();GeneratorDeviceRuntime *s=ks37_generator_device_state();
    unsigned route=CI_PASS_NATIVE;
    if(s) {
        GenGestureContext c;gd_context(s,&c);
        /* Retain native exclusion after a partial pointer transaction even
         * when the proposed gesture.active was never committed. */
        if(gd_owns_native(s)&&s->base.faults) {
            CiEffects e;ci_route_raw(object,id,before,after,1,r8(0x20005285u),&e);
            route=e.route;
        } else {
            unsigned previous_feedback=s->base.pending_feedback;
            s->base.pending_feedback=0;
            ks37_runtime_control(&s->base,object,id,before,after,r8(0x20005285u),&c,&route);
            unsigned fresh_feedback=s->base.pending_feedback;
            s->base.pending_feedback=previous_feedback;
            gd_control_feedback(s,fresh_feedback,s->base.last_control_parameter);
        }
        gd_rate_feedback12(s,object,id,before,after,r8(0x20005285u));
    }
    pd_hw_irq_restore(mask);return route;
}
void ks37_note_input_entry(uint32_t source,uint32_t packed,uint32_t filter,uint32_t key) {
    uint32_t mask=pd_hw_irq_save();GeneratorDeviceRuntime *s=ks37_generator_device_state();
    if(s) {
        IhEffects e;ih_receive_hook(&s->base.input,source,packed,filter,key,&e);
        if(s->base.observed_notes!=UINT32_MAX)++s->base.observed_notes;
        gd_input_effects(s,&e);
    }
    pd_hw_irq_restore(mask);
}
void ks37_playback_event(unsigned event,uint32_t receiver,unsigned value) {
    uint32_t mask=pd_hw_irq_save();GeneratorDeviceRuntime *s=ks37_generator_device_state();
    gd_native_event(s,event,receiver,value);pd_hw_irq_restore(mask);
}
void ks37_playback_cycle_observe(uint32_t receiver,unsigned step,unsigned wrap,uint32_t phase) {
    uint32_t mask=pd_hw_irq_save();GeneratorDeviceRuntime *s=ks37_generator_device_state();
    if(s) {
        PdResult result=pd_step_witness_locked(&s->device,receiver,step,wrap,phase);
        if(s->device.phase==PD_FAULT||result==PD_STOCK_GUARD||result==PD_INTERLEAVE) {
            gd_trace_fault11(s,78,0);s->base.faults|=GD_PUBLICATION_FAULT;
        }
    }
    pd_hw_irq_restore(mask);
}
unsigned ks37_playback_allow(uint32_t receiver,unsigned action) {
    uint32_t mask=pd_hw_irq_save();GeneratorDeviceRuntime *s=ks37_generator_device_state();
    unsigned allow=!gd_owns_native(s)||(s->transport_permit&&!pd_hw_ipsr()&&
        receiver==0x20002becu&&action<=3);
    if(!allow&&s&&s->quarantined!=UINT32_MAX)++s->quarantined;
    pd_hw_irq_restore(mask);return allow;
}
void gd_hw_transport(GeneratorDeviceRuntime *s,unsigned request) {
    uint32_t mask=pd_hw_irq_save();
    if(!s||!gd_owns_native(s)||pd_hw_ipsr()||mask||
       r32(0x20001098u)!=0x20002becu||r32(0x20001124u)!=0x20002ddcu||
       r8(0x20002bf8u)||r8(0x20002deeu)) {
        if(s)do{gd_trace_fault11(s,75,0);s->base.faults|=GD_NATIVE_FAULT;}while(0);pd_hw_irq_restore(mask);return;
    }
    unsigned ui=r8(0x20002decu), action;
    if(request==GG_TRANSPORT_STOP)action=0;
    else if(request!=GG_TRANSPORT_TOGGLE||ui>2) {
        do{gd_trace_fault11(s,75,0);s->base.faults|=GD_NATIVE_FAULT;}while(0);pd_hw_irq_restore(mask);return;
    } else action=ui==0?1:ui==1?3:2;
    s->transport_permit=1;pd_hw_irq_restore(mask);
    /* IRQ0 is necessary for normal stock NoteOff/output work. The entry gate
     * excludes external callers throughout this main-only permit. */
    ((void (*)(uint32_t,unsigned))(uintptr_t)0x08012335u)(0x20002becu,action);
    mask=pd_hw_irq_save();s->transport_permit=0;
    w8(0x20002decu,action==0?0:action==2?1:2);
    w8(0x20002df8u,0);
    pd_hw_irq_restore(mask);
}
void ks37_generator_main_before(void) {gd_main_before(ks37_generator_device_state());}
void ks37_generator_main_after(void) {gd_main_after(ks37_generator_device_state());}

/* Whole-entry gates preserve native work outside GEN. Root patches only
 * individually inspected entries. A denied native writer never borrows an
 * owned sequence buffer, including after a late fault. */
unsigned ks37_generator_native_block(uint32_t receiver,unsigned kind) {
    (void)receiver;(void)kind;
    uint32_t mask=pd_hw_irq_save();GeneratorDeviceRuntime *s=ks37_generator_device_state();
    unsigned block=gd_owns_native(s);
    if(block&&s->quarantined!=UINT32_MAX)++s->quarantined;
    pd_hw_irq_restore(mask);return block;
}

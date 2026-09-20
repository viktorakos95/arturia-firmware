#include "generator_device_runtime.h"
#include "fault_trace11.h"
#include "generator_diagnostics.h"

static void zero(void *p,unsigned n) {uint8_t *b=p;while(n--)*b++=0;}
static void increment(uint32_t *v) {if(*v!=UINT32_MAX)++*v;}
int gd_owns_native(const GeneratorDeviceRuntime *s) {
    return s&&(s->base.controller.gesture.active||s->device.phase!=PD_OFF);
}
int gd_init(GeneratorDeviceRuntime *s,uint32_t capacity) {
    if(!s||capacity<sizeof(*s))return 0;
    zero(s,sizeof(*s));
    if(!ks37_runtime_init(&s->base,sizeof(s->base)))return 0;
    bda_init(&s->buttons);
    input_device_init(&s->inputs,ID_PROFILE_STOCK_RM0008);
    if(!pd_init(&s->device,(uint32_t)(uintptr_t)s->base.sequence[0],
                          (uint32_t)(uintptr_t)s->base.sequence[1]))return 0;
    s->button_binding=(BdaBinding){&s->buttons,&s->base.ownership,&s->base.admission,
        &s->base.observer,&s->base.controller,s,gd_context,gd_delivery,gd_mode_transaction};
    s->cpuid=gd_hw_cpuid();
    /* ARM implementer, Cortex-M3 part. This establishes the architecture only;
     * ID_PROFILE_STOCK_RM0008 is a pinned register-layout build profile. */
    if((s->cpuid&0xff00fff0u)!=0x4100c230u)do{gd_trace_fault11(s,74,0);s->base.faults|=GD_BAD_PROFILE;}while(0);
    return 1;
}
void gd_context(void *context,GenGestureContext *out) {
    GeneratorDeviceRuntime *s=context;zero(out,sizeof(*out));
    if(!s||!gd_hw_context(s,out))return;
    ih_gesture_notes(&s->base.input,out);
    if(!input_device_physical_known(&s->inputs,s->inputs.frame_epoch))
        out->known&=(uint16_t)~GG_K_NOTES;
    else if(!input_device_physical_empty(&s->inputs,s->inputs.frame_epoch)) {
        out->known|=GG_K_NOTES;out->notes_held=1;
    }
    if(s->base.faults||s->buttons.faults||s->transport_count||s->transport_permit) {
        out->known|=GG_K_PENDING;out->pending=1;
    }
}
static void feedback(GeneratorDeviceRuntime *s,const GenControllerEffects *e) {
    /* Presentation follows the latest serialized event, not an OR of stale
     * knob/command notices. Musical queues and boundary feedback stay in core. */
    const unsigned transient=GC_PARAMETER|GC_QUEUED|GC_APPLIED|GC_REJECTED|GC_CAPTURE_COMPLETE;
    if(e->flags&transient) {
        s->base.pending_feedback&=~transient;
        s->feedback_action=0;
    }
    s->base.pending_feedback|=e->flags;
    if(e->flags&GC_CAPTURE_COMPLETE)s->panel.capture_complete_count=e->count;
    if(e->flags&GC_REQUEST_STOPPED_PUBLICATION)s->base.stopped_publication_requested=1;
    if(e->flags&(GC_PARAMETER|GC_QUEUED|GC_APPLIED|GC_REJECTED|GC_CAPTURE_COMPLETE)) {
        s->feedback_dirty=1;s->feedback_tick=gd_hw_tick();
    }
}
void gd_control_feedback(GeneratorDeviceRuntime *s,unsigned flags,unsigned parameter) {
    if(!s)return;
    GenControllerEffects e={0};e.flags=flags;
    if(flags&GC_PARAMETER)s->last_parameter=parameter;
    feedback(s,&e);
}
void gd_input_effects(void *context,const IhEffects *edges) {
    GeneratorDeviceRuntime *s=context;if(!s||!edges)return;
    if(edges->faults||edges->known_sources!=IH_ALL_SOURCES) {
        gen_controller_cancel_prefix(&s->base.controller);
        if(gd_owns_native(s))do{gd_trace_fault11(s,71,0);s->base.faults|=DR_INPUT_FAULT;}while(0);
    }
    if(s->base.faults)return;
    GenGestureContext c;gd_context(s,&c);
    for(unsigned pitch=0;pitch<128;++pitch) {
        uint32_t bit=1u<<(pitch&31);unsigned word=pitch>>5;
        if((edges->up[word]|edges->down[word])&bit) {
            GenControllerEffects effects;
            gen_controller_note(&s->base.controller,pitch,!!(edges->down[word]&bit),&c,&effects);
            feedback(s,&effects);
        }
    }
}
void gd_delivery(void *context,const BdaDelivery *d) {
    GeneratorDeviceRuntime *s=context;if(!s||!d)return;
    if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
    if(d->effects.flags&GC_PARAMETER)s->last_parameter=d->effects.parameter;
    feedback(s,&d->effects);
    for(unsigned i=0;i<d->effects.gesture.count;++i) {
        const GenGestureEffect *e=&d->effects.gesture.items[i];
        if(e->type==GG_EFFECT_TRANSPORT) {
            if(s->transport_count==GD_TRANSPORT_CAPACITY)do{gd_trace_fault11(s,76,0);s->base.faults|=GD_TRANSPORT_OVERFLOW;}while(0);
            else s->transport[s->transport_count++]=e->value;
        }
        if(e->type==GG_EFFECT_MODE) {increment(&s->mode_changes);s->feedback_dirty=1;}
        if(e->type==GG_EFFECT_REJECT) {
            GenControllerEffects rejected={0};rejected.flags=GC_REJECTED;
            feedback(s,&rejected);
            increment(&s->refused);s->feedback_dirty=1;s->feedback_tick=gd_hw_tick();
            unsigned code=s->mode_diagnostic?s->mode_diagnostic:gd_diagnostic_reason(s,e->value);
            gp_diagnostic(&s->panel,code,s->feedback_tick);s->mode_diagnostic=0;
        }
        if(e->type==GG_EFFECT_COMMAND)s->feedback_action=e->value;
    }
    if(d->mode_status==BDA_TX_FAULT_AFTER_PUBLICATION)do{gd_trace_fault11(s,78,0);s->base.faults|=GD_PUBLICATION_FAULT;}while(0);
}
void gd_publication_commit(void *context,unsigned kind) {
    GeneratorDeviceRuntime *s=context;
    if(kind==PD_PREPARE_PUBLISH||kind==PD_PREPARE_CYCLE) {
        unsigned manual_notice=s->prepared.core.pending_count&&
            gen_feedback(&s->prepared.core.pending_feedback).valid;
        /* Musical work was evaluated outside the mask into the exact encoded
         * proposal. Publish that state only after the native pointer/timing
         * transaction succeeded; never run auto evolution a second time. */
        s->base.controller.core=s->prepared.proposed;
        GenControllerEffects e;gen_controller_publication_applied(&s->base.controller,&e);
        /* A parameter-only publication can precede the first display read in
         * this main frame. Keep that knob event and its original timestamp.
         * Automatic variation also leaves the current knob/RATE notice alone;
         * only explicit Vary/Commit/Reset/capture has an applied text notice. */
        if(manual_notice)feedback(s,&e);
        increment(&s->publications);
        s->base.stopped_publication_requested=0;
    }
}
static void mode_commit(void *context,unsigned kind) {
    GeneratorDeviceRuntime *s=context;
    (void)kind;
    /* Pointer/native service succeeded; perform only the acknowledged D0 store.
     * BDA commits its preflighted controller/ownership copies after this returns. */
    s->mode_d_zero=(uint8_t)gd_hw_finish_chord();
}
unsigned gd_mode_transaction(void *context,BdaModeTransaction *tx) {
    GeneratorDeviceRuntime *s=context;
    if(!s||!tx)return BDA_TX_REFUSED;
    if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
    s->mode_diagnostic=0;
    if(s->base.faults||s->buttons.faults||s->transport_count) {
        s->mode_diagnostic=(uint8_t)gd_diagnostic_reason(s,GG_TRANSITION_PENDING);
        return BDA_TX_REFUSED;
    }
    unsigned wanted=tx->new_active?PD_PREPARE_ENTER:PD_PREPARE_LEAVE;
    if(!s->prepared.ready||s->prepared.kind!=wanted) {
        unsigned code=pd_diagnostic_locked(&s->device);
        s->mode_diagnostic=(uint8_t)(code?code:!s->prepared.ready?46:47);
        return BDA_TX_REFUSED;
    }
    s->mode_d_zero=0;
    PdResult result=pd_apply_prepared_locked(&s->device,&s->prepared,
        &s->base.controller.core,mode_commit,s);
    if(result==PD_OK||result==PD_PUBLISHED) {
        if(tx->finish_hold&&!s->mode_d_zero) {
            do{gd_trace_fault11(s,75,0);s->base.faults|=GD_NATIVE_FAULT;}while(0);return BDA_TX_FAULT_AFTER_PUBLICATION;
        }
        return BDA_TX_COMMITTED|(s->mode_d_zero?BDA_TX_D_ZERO:0);
    }
    if(s->device.phase==PD_FAULT) {
        do{gd_trace_fault11(s,78,0);s->base.faults|=GD_PUBLICATION_FAULT;}while(0);
        return BDA_TX_FAULT_AFTER_PUBLICATION|(s->mode_d_zero?BDA_TX_D_ZERO:0);
    }
    unsigned code=pd_diagnostic_locked(&s->device);
    s->mode_diagnostic=(uint8_t)(code?code:result==PD_STALE_PREPARED?48:49);
    return BDA_TX_REFUSED;
}
void gd_native_event(GeneratorDeviceRuntime *s,unsigned event,uint32_t receiver,unsigned value) {
    if(!s)return;
    if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
    PdResult result=pd_event_prepared_locked(&s->device,&s->prepared,&s->base.controller.core,
                                           event,receiver,value,gd_publication_commit,s);
    if(s->device.phase==PD_FAULT||result==PD_PUBLICATION_FAULT||result==PD_COMMIT_FAULT||
       (s->device.phase==PD_ACTIVE&&result==PD_STOCK_GUARD))
        do{gd_trace_fault11(s,78,0);s->base.faults|=GD_PUBLICATION_FAULT;}while(0);
}
void gd_main_before(GeneratorDeviceRuntime *s) {
    if(!s)return;
    uint32_t mask=pd_hw_irq_save();
    if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
    if(s->main_open) {do{gd_trace_fault11(s,75,0);s->base.faults|=GD_NATIVE_FAULT;}while(0);pd_hw_irq_restore(mask);return;}
    s->main_open=1;increment(&s->main_frames);
    if(!s->base.faults&&!s->buttons.faults&&s->device.phase!=PD_FAULT) {
        GenGestureContext c;GenControllerEffects e={0};gd_context(s,&c);
        gen_controller_mode_observe(&s->base.controller,gd_hw_mode_detent(),&c,&e);
        if(e.flags&GC_PARAMETER)s->last_parameter=e.parameter;
        feedback(s,&e);
    }
    if((s->base.faults||s->buttons.faults)&&gd_owns_native(s)&&!s->fault_stop_attempted) {
        /* Stop consumes the native consumer's real active-note list. Retain
         * the private buffers and the fault; never fake native NoteOff by
         * clearing N. Musical commands queued before the fault are cancelled. */
        s->transport[0]=GG_TRANSPORT_STOP;s->transport_count=1;s->fault_stop_attempted=1;
    }
    pd_hw_irq_restore(mask);
    for(unsigned n=0;n<GD_TRANSPORT_CAPACITY;++n) {
        mask=pd_hw_irq_save();
        if(!s->transport_count) {pd_hw_irq_restore(mask);break;}
        unsigned request=s->transport[0];
        for(unsigned i=1;i<s->transport_count;++i)s->transport[i-1]=s->transport[i];
        --s->transport_count;pd_hw_irq_restore(mask);
        if(!s->base.faults||request==GG_TRANSPORT_STOP)gd_hw_transport(s,request);
    }
}
void gd_main_after(GeneratorDeviceRuntime *s) {
    if(!s)return;
    uint32_t mask=pd_hw_irq_save();s->main_open=0;
    unsigned fault=s->base.faults||s->buttons.faults;
    unsigned active=s->base.controller.gesture.active;
    unsigned pending=s->base.controller.core.pending_count;
    unsigned ongoing=s->device.bridge.transport==PB_RUNNING||s->device.bridge.transport==PB_PAUSED;
    const GenParams *parameters=&s->base.controller.core.current.params;
    /* Event phases advance at each heard cycle even when E00 holds material.
     * A silent rhythm does not spend RNG or advance event indices. */
    unsigned evolving=parameters->density||
        s->base.controller.core.current.auto_counter;
    pd_hw_irq_restore(mask);
    if(!fault) {
        PdResult result;
        if(!active)result=pd_prepare_enter(&s->device,&s->prepared,&s->base.controller.core);
        else if(ongoing&&(pending||evolving))result=pd_prepare_cycle(&s->device,&s->prepared,&s->base.controller.core);
        else if(pending)result=pd_prepare_publish(&s->device,&s->prepared,&s->base.controller.core);
        else result=pd_prepare_leave(&s->device,&s->prepared,&s->base.controller.core);
        mask=pd_hw_irq_save();
        if(result==PD_OK&&active&&pending) {
            GenGestureContext c;gd_context(s,&c);
            if((c.known&GG_K_TRANSPORT)&&c.transport==GG_STOPPED) {
                result=pd_apply_prepared_locked(&s->device,&s->prepared,&s->base.controller.core,gd_publication_commit,s);
                if(result==PD_STOCK_GUARD)do{gd_trace_fault11(s,78,0);s->base.faults|=GD_PUBLICATION_FAULT;}while(0);
            }
        }
        if(s->device.phase==PD_FAULT)do{gd_trace_fault11(s,78,0);s->base.faults|=GD_PUBLICATION_FAULT;}while(0);
        pd_hw_irq_restore(mask);
    }
    gd_hw_feedback(s);
}

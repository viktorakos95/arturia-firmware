#include "button_device_adapter.h"
#include <stddef.h>

_Static_assert(sizeof(BdaPoll)==64,"assembly poll capsule");
_Static_assert(offsetof(BdaPoll,ticket)==12,"assembly capsule ticket");
static void zero(void *ptr,unsigned n) { uint8_t *p=ptr;while(n--)*p++=0; }
static void copy(void *to,const void *from,unsigned n) {
    volatile uint8_t *d=to;const uint8_t *s=from;while(n--)*d++=*s++;
}
static int bound(const BdaBinding *b) {
    return b&&b->state&&b->ownership&&b->admission&&b->observer&&b->controller;
}
static int valid(BdaBinding *b,BdaPoll *p,uint32_t object) {
    return bound(b)&&p&&p->magic==BDA_MAGIC&&p->object==object&&
        p->id<BO_BUTTON_COUNT&&p->serial&&p->serial==b->state->serial;
}
void bda_init(BdaState *s) {zero(s,sizeof(*s));}
void bda_constructed(BdaBinding *b,uint32_t object,unsigned id) {
    if(!bound(b)||!object||id>=BO_BUTTON_COUNT)return;
    unsigned mask=1u<<id;
    if((b->state->constructors&mask)||b->ownership->buttons[id].initialized) {
        b->state->faults|=BDA_BAD_LIFETIME;return;
    }
    b->state->constructors|=(uint16_t)mask;b->state->objects[id]=object;
}
static unsigned gesture_id(unsigned id) {
    switch(id) {case 1:return GG_SHIFT;case 5:return GG_RECORD;case 6:return GG_STOP;
        case 7:return GG_PLAY;case 8:return GG_CHORD;default:return GG_BUTTON_COUNT;}
}
static void deliver(BdaBinding *b,const BdaDelivery *d) {if(b->deliver)b->deliver(b->user,d);}
static unsigned native_policy(const GenControllerEffects *effects) {
    for(unsigned i=0;i<effects->gesture.count;++i)
        if(effects->gesture.items[i].type==GG_EFFECT_NATIVE)return BO_PASS_NATIVE;
    return BO_SUPPRESS;
}
static int finish_ready(const BoState *ownership,BoTicket t) {
    const BoButton *c=&ownership->buttons[BO_CHORD_ID];
    /* Exact bo_ack_finished_chord_hold preconditions, evaluated on a private
     * proposal. No actual-D0 proof is fabricated during this preflight. */
    return t.id==BO_CHORD_ID&&t.down==1&&c->has_pair&&t.generation==c->generation&&
        !c->in_flight&&!c->blocked&&c->physical_down&&!c->up_seen&&c->chord_ack&&
        c->native_held&&(c->route_resolved&BO_DOWN_BIT);
}
static int refuse_mode(BdaBinding *b,unsigned button,GenGestureContext context,BdaDelivery *d) {
    /* Live controller was not changed by the proposal. Route the physical
     * Record DOWN exactly once, as a sticky rejected combination. */
    context.known|=GG_K_PENDING;context.pending=1;
    if(!gen_controller_button(b->controller,button,d->ticket.down,&context,&d->effects))return 0;
    d->native_policy=BO_SUPPRESS;d->mode_status=BDA_TX_REFUSED;d->mode_receipts=0;
    return bo_route_resolve(b->ownership,d->ticket,BO_SUPPRESS)==BO_NONE;
}
static int route_controller(BdaBinding *b,unsigned button,BdaDelivery *d) {
    GenGestureContext context={0};if(b->context)b->context(b->user,&context);
    /* Includes Octave held at bootstrap, before controller edge history. */
    if(b->ownership->buttons[2].physical_down||b->ownership->buttons[3].physical_down){
        context.known|=GG_K_PENDING;context.pending=1;
    }
    GenController controller;copy(&controller,b->controller,sizeof(controller));
    if(!gen_controller_button(&controller,button,d->ticket.down,&context,&d->effects))return 0;
    unsigned mode=0,finish_hold=0;
    for(unsigned i=0;i<d->effects.gesture.count;++i) {
        const GenGestureEffect *e=&d->effects.gesture.items[i];
        if(e->type==GG_EFFECT_MODE)++mode;
        if(e->type==GG_EFFECT_FINISH_HOLD)++finish_hold;
    }
    d->native_policy=(uint8_t)native_policy(&d->effects);
    if(!mode) {
        if(finish_hold)return 0;
        if(bo_route_resolve(b->ownership,d->ticket,(BoDecision)d->native_policy)!=BO_NONE)return 0;
        copy(b->controller,&controller,sizeof(controller));return 1;
    }
    BoState ownership;copy(&ownership,b->ownership,sizeof(ownership));
    BoTicket chord={0};
    const BoButton *chord_owner=&ownership.buttons[BO_CHORD_ID];
    if(mode!=1||finish_hold>1||button!=GG_RECORD||!d->ticket.down||d->ticket.id!=BA_RECORD_ID||
       d->native_policy!=BO_SUPPRESS||controller.gesture.active==b->controller->gesture.active||
       bo_current_ticket(&ownership,BO_CHORD_ID,&chord)!=BO_NONE||!chord_owner->has_pair||
       chord.down!=1||chord_owner->in_flight||chord_owner->up_seen||
       !(chord_owner->route_resolved&BO_DOWN_BIT)||
       (finish_hold&&!finish_ready(&ownership,chord)))
        return refuse_mode(b,button,context,d);
    /* Resolve before ANY external effect. After callback the ownership copy is
     * immutable except for the already-preflighted actual-D0 acknowledgement. */
    if(bo_route_resolve(&ownership,d->ticket,BO_SUPPRESS)!=BO_NONE)return 0;
    BdaModeTransaction transaction={d->ticket,chord,context,b->controller,b->ownership,
        &controller,&ownership,&d->effects,(uint8_t)button,d->ticket.down,
        b->controller->gesture.active,controller.gesture.active,(uint8_t)finish_hold,(uint8_t)finish_hold};
    unsigned result=b->mode_transaction?b->mode_transaction(b->user,&transaction):BDA_TX_REFUSED;
    if(result==BDA_TX_REFUSED)return refuse_mode(b,button,context,d);
    unsigned status=result&BDA_TX_STATUS_MASK,receipt=result&BDA_TX_D_ZERO;
    unsigned protocol=(result&~(BDA_TX_STATUS_MASK|BDA_TX_D_ZERO))||
        (status!=BDA_TX_COMMITTED&&status!=BDA_TX_FAULT_AFTER_PUBLICATION)||
        (status==BDA_TX_COMMITTED&&finish_hold&&!receipt);
    if(finish_hold&&receipt) {
        /* Eligibility was checked on THIS copy, not on an old hardware sample.
         * Only now does actual D0 receipt authorize the acknowledgement. */
        if(bo_ack_finished_chord_hold(&ownership,chord,1)!=BO_NONE)protocol=1;
    }
    d->mode_receipts=(uint8_t)receipt;d->native_policy=BO_SUPPRESS;
    if(protocol||status==BDA_TX_FAULT_AFTER_PUBLICATION) {
        b->state->faults|=BDA_EXTERNAL_FAULT;if(protocol)b->state->faults|=BDA_MODE_PROTOCOL;
        /* Do not reroute an external partial commit as a harmless refusal. The
         * physical Record is owned/suppressed; controller stays as it was.
         * External buffer ownership stays entirely with the publication layer. */
        copy(b->ownership,&ownership,sizeof(ownership));
        zero(&d->effects,sizeof(d->effects));d->effects.flags=GC_REJECTED;
        d->effects.reject_reason=GG_TRANSITION_PENDING;d->effects.gesture.count=1;
        d->effects.gesture.items[0]=(GenGestureEffect){GG_EFFECT_REJECT,GG_TRANSITION_PENDING,0,0};
        d->mode_status=BDA_TX_FAULT_AFTER_PUBLICATION;return 1;
    }
    copy(b->ownership,&ownership,sizeof(ownership));
    copy(b->controller,&controller,sizeof(controller));
    d->mode_status=BDA_TX_COMMITTED;return 1;
}
static int route_octave(BdaBinding *b,BdaDelivery *d) {
    GenGestureContext context={0};if(b->context)b->context(b->user,&context);
    GenController controller;copy(&controller,b->controller,sizeof(controller));
    unsigned result=gen_controller_octave(&controller,d->ticket.id==3,d->ticket.down,&context,&d->effects);
    if(result==GC_OCTAVE_INVALID)return 0;
    d->native_policy=result==GC_OCTAVE_OWNED?BO_SUPPRESS:BO_PASS_NATIVE;
    if(bo_route_resolve(b->ownership,d->ticket,(BoDecision)d->native_policy)!=BO_NONE)return 0;
    copy(b->controller,&controller,sizeof(controller));return 1;
}
static void drain(BdaBinding *b,unsigned id) {
    for(unsigned n=0;n<2;++n) {
        BaRoute route;BaDecision choice=ba_take_route(b->admission,b->ownership,id,&route);
        if(choice==BA_NONE||choice==BA_WAIT_DEBT)return;
        if(choice==BA_INVALID) {b->state->faults|=BDA_ROUTE_ERROR;return;}
        BdaDelivery d;zero(&d,sizeof(d));d.ticket=route.route.ticket;
        d.admission=(uint8_t)choice;d.native_effect_already_done=route.route.native_effect_already_done;
        if(choice==BA_REJECTED_COMBO) {
            d.effects.gesture.count=1;d.effects.gesture.items[0]=route.feedback;
            d.effects.flags=GC_REJECTED;d.native_policy=BO_SUPPRESS;deliver(b,&d);continue;
        }
        if(choice==BA_CONSUMED_RELEASE) {d.native_policy=BO_SUPPRESS;deliver(b,&d);continue;}
        if(choice==BA_NATIVE_ONLY) {d.native_policy=BO_PASS_NATIVE;deliver(b,&d);continue;}
        unsigned button=gesture_id(id);
        if(b->state->faults&BDA_EXTERNAL_FAULT) {
            /* Preserve real native DOWN debt, but never introduce new native
             * effects after a possibly-performed external publication. */
            d.native_policy=route.route.native_effect_already_done?BO_PASS_NATIVE:BO_SUPPRESS;
            d.mode_status=BDA_TX_FAULT_AFTER_PUBLICATION;
            if(bo_route_resolve(b->ownership,d.ticket,(BoDecision)d.native_policy)!=BO_NONE) {
                b->state->faults|=BDA_ROUTE_ERROR;return;
            }
        } else if(button<GG_BUTTON_COUNT) {
            if(!route_controller(b,button,&d)) {b->state->faults|=BDA_ROUTE_ERROR;return;}
        } else if(id==2||id==3) {
            if(!route_octave(b,&d)) {b->state->faults|=BDA_ROUTE_ERROR;return;}
        } else {
            if(d.ticket.down)gen_controller_cancel_prefix(b->controller);
            d.native_policy=BO_PASS_NATIVE;
            if(bo_route_resolve(b->ownership,d.ticket,BO_PASS_NATIVE)!=BO_NONE) {
                b->state->faults|=BDA_ROUTE_ERROR;return;
            }
        }
        deliver(b,&d);
    }
}
void bda_observed(BdaBinding *b,BdaPoll *p,uint32_t object,unsigned id,
                  Ks37ButtonObservation observation,unsigned stable_up) {
    if(!p)return;zero(p,sizeof(*p));
    if(!bound(b)||!object||id>=BO_BUTTON_COUNT||observation==KS37_BUTTON_INVALID)return;
    if(b->state->serial==UINT32_MAX) {b->state->faults|=BDA_BAD_LIFETIME;return;}
    p->magic=BDA_MAGIC;p->object=object;p->id=id;p->serial=++b->state->serial;
    if(stable_up)p->flags|=BDA_STABLE_UP;
    if(observation==KS37_BUTTON_INITIAL_UP||observation==KS37_BUTTON_INITIAL_DOWN)
        bo_sync(b->ownership,id,observation==KS37_BUTTON_INITIAL_DOWN);
    else if(observation==KS37_BUTTON_UP||observation==KS37_BUTTON_DOWN) {
        unsigned down=observation==KS37_BUTTON_DOWN;
        bo_observe(b->ownership,id,down,down&&id==BO_CHORD_ID&&!b->controller->gesture.active,&p->ticket);
        if(id==BA_RECORD_ID&&down)b->state->bootstrap_record_suppressed=0;
        if(id==BA_RECORD_ID&&down&&b->ownership->buttons[BO_CHORD_ID].physical_down&&
           (!b->ownership->buttons[id].has_pair||b->ownership->buttons[id].blocked))
            b->state->bootstrap_record_suppressed=1;
    }
    if(id==BA_RECORD_ID&&b->state->bootstrap_record_suppressed) {
        p->flags|=BDA_BLOCK_RECORD;
    }
    const BoButton *owner=&b->ownership->buttons[id];
    if(owner->has_pair&&!owner->blocked&&bo_current_ticket(b->ownership,id,&p->ticket)==BO_NONE)
        p->flags|=BDA_HAVE_TICKET;
    else p->flags|=BDA_UNTRACKED;
    if(owner->has_pair&&owner->blocked)p->flags|=BDA_BLOCK_DEBT;
    drain(b,id);
}
unsigned bda_native_pre(BdaBinding *b,BdaPoll *p,uint32_t object,unsigned down) {
    if(!bound(b)||!p)return BDA_PASS; /* Explicitly unrelated stock caller. */
    if(!valid(b,p,object)||down>1) {b->state->faults|=BDA_BAD_CALL;return BDA_CONSUME;}
    if(b->state->faults&BDA_EXTERNAL_FAULT)return BDA_CONSUME;
    if(p->flags&(BDA_BLOCK_RECORD|BDA_BLOCK_DEBT))return BDA_CONSUME;
    if(p->flags&BDA_CALL_OPEN) {
        b->state->faults|=BDA_BAD_CALL;p->flags|=BDA_CALL_POISON;return BDA_CONSUME;
    }
    if(!(p->flags&BDA_HAVE_TICKET))return BDA_PASS; /* Explicit bootstrap stock. */
    BoDecision decision=bo_native_pre(b->ownership,p->ticket,down,&p->call);
    if(decision!=BO_PASS_NATIVE)return BDA_CONSUME;
    p->flags|=BDA_CALL_OPEN;p->native_down=down;p->witness=0;return BDA_PASS;
}
void bda_path(BdaBinding *b,BdaPoll *p,uint32_t object,unsigned down) {
    if(valid(b,p,object)&&p->id==BO_CHORD_ID&&(p->flags&BDA_CALL_OPEN)&&p->native_down==down)
        p->witness|=1u<<down;
}
void bda_native_post(BdaBinding *b,BdaPoll *p,uint32_t object,unsigned down,BdaFields f) {
    if(!valid(b,p,object)||!(p->flags&BDA_CALL_OPEN)||p->native_down!=down||
       (p->flags&BDA_CALL_POISON))return;
    BoNativeProof proof={0};
    proof.chord_d=f.d;proof.shift_m=f.shift;proof.service_mode=f.service;
    proof.normal_chord_path_seen=(uint8_t)(down&&(p->witness&2u));
    if((f.known&BDA_FIELDS_ALL)==BDA_FIELDS_ALL) {
        unsigned held=p->id==BO_CHORD_ID?f.d:p->id==BA_RECORD_ID?f.record:p->id==1?f.shift:
            p->id==2?f.octave_minus:p->id==3?f.octave_plus:2;
        proof.effects_done=(uint8_t)(held==down);
    }
    if(bo_native_post(b->ownership,p->call,proof)!=BO_NONE)b->state->faults|=BDA_BAD_CALL;
    p->flags&=~BDA_CALL_OPEN;drain(b,p->id);
    if(p->id==BO_CHORD_ID)drain(b,BA_RECORD_ID);
}
void bda_poll_end(BdaBinding *b,BdaPoll *p,const volatile uint8_t *button,BdaFields f) {
    if(!p||!valid(b,p,p->object))return;
    if(p->flags&BDA_CALL_OPEN)b->state->faults|=BDA_BAD_CALL;
    unsigned id=p->id;
    BoButton *owner=&b->ownership->buttons[id];
    /* Positive constructor path + the COMPLETE current synchronous poll +
     * stable physical UP + no stock latch or native hold. No timeout, inferred
     * source silence, synthetic completion or all-source pending assertion. */
    if(!b->state->faults&&button&&(b->state->constructors&(1u<<id))&&
       b->state->objects[id]==p->object&&(p->flags&BDA_STABLE_UP)&&
       !(p->flags&BDA_CALL_OPEN)&&owner->initialized&&owner->bootstrap_debt&&!owner->has_pair&&
       button[13]==id&&!button[8]&&!button[9]&&!button[10]&&button[11]==1&&!button[12]&&button[18]==1&&
       f.known==BDA_FIELDS_ALL&&!f.d&&!f.record&&!f.shift&&!f.service&&
       (id!=2||!f.octave_minus)&&(id!=3||!f.octave_plus))
        bo_ack_initial_idle(b->ownership,id,1);
    p->magic=0;
}

#if defined(__arm__) || defined(__thumb__)
static uint32_t lock(void) {uint32_t m;__asm volatile("mrs %0,primask\n cpsid i":"=r"(m)::"memory");return m;}
static void unlock(uint32_t m) {__asm volatile("msr primask,%0"::"r"(m):"memory");}
static BdaFields fields(void) {
    BdaFields f={0};
    if(*(volatile uint32_t *)0x20001124u==0x20002ddcu) {
        f.d=*(volatile uint8_t *)0x20002de9u;f.known|=BDA_FIELD_U;
    }
    f.record=*(volatile uint8_t *)0x200010b6u;f.shift=*(volatile uint8_t *)0x200010d2u;
    f.octave_minus=*(volatile uint8_t *)0x200010d6u;f.octave_plus=*(volatile uint8_t *)0x200010d0u;
    f.service=*(volatile uint8_t *)0x20005285u;f.known|=BDA_FIELD_GLOBALS;return f;
}
void ks37_button_constructor_entry(uint32_t object,unsigned id) {
    uint32_t m=lock();bda_constructed(ks37_button_device_binding(),object,id);unlock(m);
}
void ks37_button_poll_entry(BdaPoll *p,uint32_t object,unsigned released) {
    uint32_t m=lock();BdaBinding *b=ks37_button_device_binding();
    if(bound(b)) {
        const volatile uint8_t *button=(const volatile uint8_t *)(uintptr_t)object;
        Ks37ButtonObservation o=ks37_button_observe(b->observer,button,released);
        bda_observed(b,p,object,button[13],o,button[18]==1&&released==1);
    } else zero(p,sizeof(*p));
    unlock(m);
}
unsigned ks37_button_native_pre_entry(BdaPoll *p,uint32_t object,unsigned down) {
    uint32_t m=lock();unsigned result=bda_native_pre(ks37_button_device_binding(),p,object,down);unlock(m);return result;
}
void ks37_button_native_post_entry(BdaPoll *p,uint32_t object,unsigned down) {
    uint32_t m=lock();bda_native_post(ks37_button_device_binding(),p,object,down,fields());unlock(m);
}
void ks37_button_path_entry(BdaPoll *p,uint32_t u,unsigned down) {
    uint32_t m=lock();
    if(p&&u==0x20002ddcu&&*(volatile uint32_t *)0x20001124u==u)
        bda_path(ks37_button_device_binding(),p,p->object,down);
    unlock(m);
}
void ks37_button_poll_end_entry(BdaPoll *p) {
    uint32_t m=lock();
    bda_poll_end(ks37_button_device_binding(),p,p?(const volatile uint8_t *)(uintptr_t)p->object:0,fields());unlock(m);
}
#endif

#include "generator_controller.h"

static void clear(void *ptr, unsigned n) {
    uint8_t *p=ptr; for(unsigned i=0;i<n;++i) p[i]=0;
}
static unsigned parameter(GenParams p,unsigned i) {
    switch(i) {
    case GCP_LENGTH:return p.length;case GCP_DENSITY:return p.density;
    case GCP_ROTATION:return p.rotation;case GCP_EVOLVE:return p.evolve;
    case GCP_MOTION:return p.motion;case GCP_ACCENT:return p.accent;
    case GCP_ACCENT_PERIOD:return p.accent_period;
    case GCP_RANGE:return p.range;case GCP_AUTO:return p.auto_period;
    default:return 0;
    }
}
GenParams gen_controller_parameters(const GenController *s) {
    return s->core.pending_count?s->core.next_current.params:s->core.current.params;
}
unsigned gen_controller_parameter_value(const GenController *s,unsigned i) {
    return s&&(i<GC_CONTROLS||i==GCP_RANGE)?parameter(gen_controller_parameters(s),i):0;
}
static unsigned maximum(GenParams p,unsigned i) {
    return i==GCP_LENGTH?63:i==GCP_DENSITY?p.length:
        i==GCP_ROTATION?p.length-1:i==GCP_MOTION?31:i==GCP_ACCENT_PERIOD?15:i==GCP_AUTO?4:99;
}
static unsigned mapped(GenParams p,unsigned i,unsigned unit) {
    unsigned value=unit*maximum(p,i)/127;
    static const uint8_t periods[5]={0,1,2,4,8};
    return (i==GCP_LENGTH||i==GCP_MOTION||i==GCP_ACCENT_PERIOD)?value+1:i==GCP_AUTO?periods[value]:value;
}
static unsigned unit_for(GenParams p,unsigned i) {
    unsigned value=parameter(p,i),max=maximum(p,i);
    if(i==GCP_LENGTH||i==GCP_MOTION||i==GCP_ACCENT_PERIOD)--value;
    if(i==GCP_AUTO)value=value==8?4:value==4?3:value;
    return max?(value*127+max-1)/max:0;
}
static void rearm(GenController *s) {
    for(unsigned i=0;i<GC_CONTROLS;++i)gen_control_reset(&s->knobs[i],s->knobs[i].value);
}
static void restore(GenController *s,GenParams p) {
    for(unsigned i=0;i<GC_CONTROLS;++i)gen_control_reset(&s->knobs[i],unit_for(p,i));
}
static void physical(GenController *s,unsigned index,unsigned absolute) {
    for(unsigned page=0;page<2;++page){
        GenControl *k=&s->knobs[index+page*4];
        k->physical=(uint8_t)absolute;k->physical_valid=1;
    }
}
int gen_controller_init(GenController *s,const uint8_t *notes,unsigned count,GenParams p,uint32_t rng) {
    if(!s) return 0;
    /* Validate/copy through core before changing any other controller fields.
     * gen_init itself preserves input aliases within the core state. */
    if(!gen_init(&s->core,notes,count,p,rng)) return 0;
    gen_gesture_init(&s->gesture,0);
    clear(s->knobs,sizeof(s->knobs)); clear(s->held_notes,sizeof(s->held_notes));
    s->held_count=s->capture_count=s->pending_restore=0;
    clear(s->capture,sizeof(s->capture));s->mode_seen=s->mode_valid=0;
    s->octave_held=s->octave_owned=0;
    restore(s,s->core.current.params);
    return 1;
}
void gen_controller_cancel_prefix(GenController *s) { gen_gesture_cancel_prefix(&s->gesture); }
static void queue_result(GenController *s,const GenGestureContext *c,GenControllerEffects *e,int success) {
    if(!success) { e->flags|=GC_REJECTED; e->reject_reason=GC_QUEUE_FULL; return; }
    e->flags|=GC_QUEUED;
    if(c && (c->known&GG_K_TRANSPORT) && c->transport==GG_STOPPED)
        e->flags|=GC_REQUEST_STOPPED_PUBLICATION;
    (void)s;
}
static void command(GenController *s,unsigned action,const GenGestureContext *c,GenControllerEffects *e) {
    GenAction core_action=action==GG_VARY?GEN_VARY:action==GG_COMMIT?GEN_COMMIT:GEN_RESET;
    int accepted=gen_command(&s->core,core_action);
    if(accepted && core_action==GEN_RESET) {
        s->pending_restore=1; restore(s,s->core.seed.params);
    }
    queue_result(s,c,e,accepted);
}
int gen_controller_button(GenController *s,unsigned button,unsigned down,
                          const GenGestureContext *context,GenControllerEffects *e) {
    if(!s || !e || button>=GG_BUTTON_COUNT || down>1) return 0;
    clear(e,sizeof(*e));
    GenGestureContext c={0}; if(context) c=*context;
    /* Known activity can deny entry even if the native source coverage is unknown;
     * an empty local bitmap can NEVER establish notesHeld==0 by itself. */
    if(s->held_count) {c.notes_held=s->held_count;c.known|=GG_K_NOTES;}
    if(s->capture_count) {c.capture=1;c.known|=GG_K_CAPTURE;}
    if(s->core.pending_count) {c.pending=1;c.known|=GG_K_PENDING;}
    if(s->octave_held) {c.pending=1;c.known|=GG_K_PENDING;}
    unsigned captured=s->capture_count;
    gen_gesture_route(&s->gesture,button,down,&c,&e->gesture);
    if(!e->gesture.count) return 1;
    rearm(s);
    if(captured && down && s->gesture.owners[button].kind==GG_OWNER_COMMAND) {
        GenGestureOwner *owner=&s->gesture.owners[button];
        clear(owner,sizeof(*owner)); owner->kind=GG_OWNER_REJECTED;
        e->flags|=GC_REJECTED; e->reject_reason=GG_CAPTURE_ACTIVE;
    }
    for(unsigned i=0;i<e->gesture.count;++i) {
        GenGestureEffect *effect=&e->gesture.items[i];
        if(effect->type==GG_EFFECT_MODE) {
            s->capture_count=0; rearm(s);
        } else if(effect->type==GG_EFFECT_COMMAND) command(s,effect->value,&c,e);
    }
    if(captured && button==GG_CHORD && !down) {
        e->count=(uint8_t)captured; e->flags|=GC_CAPTURE_COMPLETE;
        int accepted=gen_new_seed(&s->core,s->capture,captured);
        s->capture_count=0;
        if(accepted) s->pending_restore=1;
        /* Captured release replaces the router's ordinary GEN hint. */
        for(unsigned i=0;i<e->gesture.count;++i) if(e->gesture.items[i].type==GG_EFFECT_HINT &&
            e->gesture.items[i].value==GG_HINT_GEN) {
            for(unsigned j=i+1;j<e->gesture.count;++j) e->gesture.items[j-1]=e->gesture.items[j];
            --e->gesture.count; break;
        }
        queue_result(s,&c,e,accepted);
    }
    return 1;
}
int gen_controller_note(GenController *s,unsigned pitch,unsigned down,
                        const GenGestureContext *context,GenControllerEffects *e) {
    if(!s || !e || pitch>127 || down>1) return 0;
    clear(e,sizeof(*e));
    uint32_t bit=1u<<(pitch&31); unsigned word=pitch>>5;
    if(!!(s->held_notes[word]&bit)==down) return 1;
    GenGestureOwner prefix=s->gesture.owners[GG_CHORD];
    unsigned may_begin=s->gesture.active && (s->gesture.held&(1u<<GG_CHORD)) &&
        prefix.kind==GG_OWNER_PREFIX && !(prefix.flags&(GG_CANCELLED|GG_ATTEMPTED));
    if(down) {s->held_notes[word]|=bit;++s->held_count;}
    else {s->held_notes[word]&=~bit;--s->held_count;}
    gen_gesture_cancel_prefix(&s->gesture);
    if(!s->gesture.active) {e->flags|=GC_NATIVE_NOTE;return 1;}
    if(!down || (!s->capture_count && !may_begin)) return 1;
    unsigned n=s->capture_count;
    for(unsigned i=0;i<n;++i) if(s->capture[i]==pitch) return 1;
    if(n==32) {e->flags|=GC_REJECTED;e->reject_reason=GG_CAPTURE_FULL;return 1;}
    unsigned at=n;
    while(at && s->capture[at-1]>pitch) {s->capture[at]=s->capture[at-1];--at;}
    s->capture[at]=(uint8_t)pitch;s->capture_count=(uint8_t)(n+1);
    e->flags|=GC_CAPTURE;e->count=s->capture_count;
    (void)context;
    return 1;
}
int gen_controller_knob(GenController *s,unsigned index,unsigned absolute,
                        const GenGestureContext *context,GenControllerEffects *e) {
    if(!s || !e || index>3 || absolute>127)return 0;
    clear(e,sizeof(*e));
    unsigned logical=index+((s->gesture.held&(1u<<GG_SHIFT))?4:0);
    GenControl *knob=&s->knobs[logical];
    if(knob->physical_valid&&knob->physical==absolute)return 1;
    gen_gesture_cancel_prefix(&s->gesture);
    if(!s->gesture.active){physical(s,index,absolute);e->flags=GC_NATIVE_KNOB;return 1;}
    GenParams p=gen_controller_parameters(s);
    unsigned old_unit=knob->value,previous=parameter(p,logical);
    GenScaleReason reason=gen_control_move(knob,absolute);
    /* Observe the shared physical knob after Scale has consumed the old sample.
     * Inactive-page history/value remain independent until their next rearm. */
    physical(s,index,absolute);
    unsigned value=mapped(p,logical,knob->value);
    e->flags=GC_PARAMETER;e->parameter=(uint8_t)logical;
    e->value=(uint8_t)value;e->scale_reason=(uint8_t)reason;
    if(value!=previous){
        unsigned mask;
        switch(logical){
        case GCP_LENGTH:p.length=(uint8_t)value;mask=GEN_LENGTH;break;
        case GCP_DENSITY:p.density=(uint8_t)value;mask=GEN_DENSITY;break;
        case GCP_ROTATION:p.rotation=(uint8_t)value;mask=GEN_ROTATION;break;
        case GCP_EVOLVE:p.evolve=(uint8_t)value;mask=GEN_EVOLVE;break;
        case GCP_MOTION:p.motion=(uint8_t)value;mask=GEN_MOTION;break;
        case GCP_ACCENT:p.accent=(uint8_t)value;mask=GEN_ACCENT;break;
        case GCP_ACCENT_PERIOD:p.accent_period=(uint8_t)value;mask=GEN_ACCENT_PERIOD;break;
        default:p.auto_period=(uint8_t)value;mask=GEN_AUTO_PERIOD;break;
        }
        int accepted=gen_params(&s->core,p,mask);
        if(!accepted){gen_control_reset(knob,old_unit);e->value=(uint8_t)previous;}
        else {
            p=gen_controller_parameters(s);e->value=(uint8_t)parameter(p,logical);
            if(logical==GCP_LENGTH){
                gen_control_reset(&s->knobs[GCP_DENSITY],unit_for(p,GCP_DENSITY));
                gen_control_reset(&s->knobs[GCP_ROTATION],unit_for(p,GCP_ROTATION));
            }
        }
        queue_result(s,context,e,accepted);
    }
    return 1;
}
unsigned gen_controller_octave(GenController *s,unsigned plus,unsigned down,
                               const GenGestureContext *context,GenControllerEffects *e) {
    if(!s||!e||plus>1||down>1)return GC_OCTAVE_INVALID;
    clear(e,sizeof(*e));
    unsigned bit=1u<<plus,held=!!(s->octave_held&bit);
    unsigned owned=!!(s->octave_owned&bit);
    if(held==down)return owned?GC_OCTAVE_OWNED:GC_OCTAVE_NATIVE;
    if(!down){
        s->octave_held&=(uint8_t)~bit;s->octave_owned&=(uint8_t)~bit;
        return owned?GC_OCTAVE_OWNED:GC_OCTAVE_NATIVE;
    }
    s->octave_held|=(uint8_t)bit;
    gen_controller_cancel_prefix(s);
    if(!s->gesture.active||!(s->gesture.held&(1u<<GG_SHIFT)))return GC_OCTAVE_NATIVE;
    s->octave_owned|=(uint8_t)bit;
    /* Shift+Octave is not a second interpretation of Chord capture. Own the
     * entire rejected pair so native shifted-octave behaviour cannot leak. */
    if(s->capture_count||(s->gesture.held&(1u<<GG_CHORD))){
        e->flags=GC_REJECTED;e->reject_reason=GG_CAPTURE_ACTIVE;
        return GC_OCTAVE_OWNED;
    }
    GenParams p=gen_controller_parameters(s);unsigned previous=p.range;
    if(plus){if(p.range<3)++p.range;}else if(p.range)--p.range;
    e->flags=GC_PARAMETER;e->parameter=GCP_RANGE;e->value=p.range;
    if(previous!=p.range){
        int accepted=gen_params(&s->core,p,GEN_RANGE);
        if(!accepted)e->value=(uint8_t)previous;
        queue_result(s,context,e,accepted);
    }
    return GC_OCTAVE_OWNED;
}
int gen_controller_mode_observe(GenController *s,unsigned detent,
                               const GenGestureContext *context,GenControllerEffects *e) {
    if(!s||!e||detent>8)return 0;
    clear(e,sizeof(*e));
    if(s->mode_valid&&s->mode_seen==detent)return 1;
    unsigned first=!s->mode_valid;
    s->mode_valid=1;s->mode_seen=(uint8_t)detent;
    if(first||!s->gesture.active)return 1;
    gen_gesture_cancel_prefix(&s->gesture);
    if(detent==8)return 1;
    GenParams p=gen_controller_parameters(s);
    e->flags=GC_PARAMETER;e->parameter=GCP_MODE;e->value=(uint8_t)detent;
    if(p.mode!=detent){
        p.mode=(uint8_t)detent;
        int accepted=gen_params(&s->core,p,GEN_MODE);
        if(!accepted)e->value=gen_controller_parameters(s).mode;
        queue_result(s,context,e,accepted);
    }
    return 1;
}
void gen_controller_publication_applied(GenController *s,GenControllerEffects *e) {
    clear(e,sizeof(*e));
    if(s->pending_restore)restore(s,s->core.current.params);
    s->pending_restore=0;e->flags=GC_APPLIED;
}
void gen_controller_applied_boundary(GenController *s,GenControllerEffects *e) {
    clear(e,sizeof(*e));
    if(!s->core.pending_count)return;
    gen_boundary(&s->core);
    gen_controller_publication_applied(s,e);
}

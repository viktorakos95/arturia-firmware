#include "input_history.h"

static void zero(void *p,unsigned n) {
    uint8_t *b=p; for(unsigned i=0;i<n;++i) b[i]=0;
}
static void start_effects(const IhState *s,IhEffects *e) {
    zero(e,sizeof(*e)); e->known_sources=s->known_sources; e->notes_held=s->held_count;
}
static void finish_effects(const IhState *s,IhEffects *e) {
    e->known_sources=s->known_sources; e->notes_held=s->held_count;
}
static uint16_t key(unsigned source,unsigned channel,unsigned pitch) {
    return (uint16_t)((source<<11)|(channel<<7)|pitch);
}
static void edge(uint32_t bits[4],unsigned pitch) { bits[pitch>>5]|=1u<<(pitch&31); }
static void lost(IhState *s,unsigned mask,unsigned reason,IhEffects *e) {
    s->known_sources&=(uint8_t)~mask;
    for(unsigned i=0;i<IH_SOURCE_COUNT;++i) if(mask&(1u<<i)) s->faults[i]|=(uint16_t)reason;
    e->faults|=(uint16_t)reason; finish_effects(s,e);
}
void ih_init(IhState *s) {
    zero(s,sizeof(*s)); s->last_filter[0]=s->last_filter[1]=0xff;
}
IhResult ih_lose_continuity(IhState *s,unsigned mask,unsigned reason,IhEffects *e) {
    if(!s || !e) return IH_INVALID;
    start_effects(s,e);
    if(!mask || (mask&~IH_ALL_SOURCES) || !reason || reason>UINT16_MAX) return IH_INVALID;
    lost(s,mask,reason,e); return IH_FAULT;
}
IhResult ih_accept_empty_source(IhState *s,unsigned source,IhBaselineProof p,IhEffects *e) {
    if(!s || !e) return IH_INVALID;
    start_effects(s,e);
    if(source>=IH_SOURCE_COUNT || p.guarantees!=IH_PROOF_ALL || !p.epoch ||
       p.epoch<=s->epochs[source]) return IH_INVALID;
    for(unsigned i=0;i<IH_CAPACITY;++i) {
        IhOwner *o=&s->owners[i];
        if(o->count && (o->key>>11)==source) {
            unsigned pitch=o->key&127;
            s->pitch_count[pitch]=(uint16_t)(s->pitch_count[pitch]-o->count);
            if(!s->pitch_count[pitch]) { --s->held_count; edge(e->up,pitch); }
            zero(o,sizeof(*o));
        }
    }
    if(source==IH_KEYBOARD) zero(s->scanner_key_owner,sizeof(s->scanner_key_owner));
    s->epochs[source]=p.epoch; s->faults[source]=0;
    s->known_sources|=(uint8_t)(1u<<source);
    e->flags|=IH_BASELINE_ACCEPTED; finish_effects(s,e); return IH_APPLIED;
}
static IhResult message(IhState *s,unsigned source,uint32_t packed,unsigned filter,IhEffects *e) {
    unsigned status=packed&255, hi=status&0xf0, channel=status&15;
    unsigned pitch=(packed>>8)&255, velocity=(packed>>16)&255;
    unsigned mask=1u<<source;
    s->seen_sources|=(uint8_t)mask;
    if(status<0x80 || (hi==0xb0 && (pitch>127 || velocity>127))) {
        lost(s,mask,IH_BAD_MESSAGE,e); return IH_FAULT;
    }
    if(status==0xff || (hi==0xb0 && pitch>=120)) {
        lost(s,mask,IH_RESET_UNRESOLVED,e); return IH_FAULT;
    }
    if(hi!=0x80 && hi!=0x90) return IH_IGNORED;
    if(pitch>127 || velocity>127) { lost(s,mask,IH_BAD_MESSAGE,e); return IH_FAULT; }
    unsigned down=hi==0x90 && velocity!=0;
    if(down && filter!=IH_OMNI && channel!=filter) return IH_IGNORED;
    uint16_t wanted=key(source,channel,pitch);
    IhOwner *found=0,*vacant=0;
    for(unsigned i=0;i<IH_CAPACITY;++i) {
        IhOwner *o=&s->owners[i];
        if(o->count && o->key==wanted) { found=o; break; }
        if(!o->count && !vacant) vacant=o;
    }
    if(down) {
        if(!found) {
            if(!vacant) { lost(s,mask,IH_CAPACITY_LOST,e); return IH_FAULT; }
            found=vacant; found->key=wanted;
        }
        if(found->count==UINT8_MAX) { lost(s,mask,IH_COUNT_OVERFLOW,e); return IH_FAULT; }
        unsigned repeated=found->count!=0;
        ++found->count;
        if(s->pitch_count[pitch]++==0) { ++s->held_count; edge(e->down,pitch); }
        if(repeated && source!=IH_KEYBOARD) { lost(s,mask,IH_REPEATED_ON,e); return IH_FAULT; }
    } else {
        if(!found) return IH_IGNORED; /* Does not guess an On or affect another source. */
        --found->count;
        if(--s->pitch_count[pitch]==0) { --s->held_count; edge(e->up,pitch); }
        if(!found->count) found->key=0;
    }
    finish_effects(s,e); return IH_APPLIED;
}
IhResult ih_receive_midi(IhState *s,unsigned source,uint32_t packed,unsigned filter,IhEffects *e) {
    if(!s || !e) return IH_INVALID;
    start_effects(s,e);
    if(source!=IH_USB && source!=IH_DIN) {
        lost(s,IH_ALL_SOURCES,IH_BAD_SOURCE,e); return IH_FAULT;
    }
    if(filter>15 && filter!=IH_OMNI) {
        lost(s,1u<<source,IH_BAD_MESSAGE,e); return IH_FAULT;
    }
    if(s->last_filter[source]!=0xff && s->last_filter[source]!=filter) e->flags|=IH_FILTER_CHANGED;
    s->last_filter[source]=(uint8_t)filter;
    return message(s,source,packed,filter,e);
}
IhResult ih_receive_scanner(IhState *s,uint32_t packed,unsigned scanner_key,IhEffects *e) {
    if(!s || !e) return IH_INVALID;
    start_effects(s,e);
    if(scanner_key>36) { lost(s,1u<<IH_KEYBOARD,IH_BAD_MESSAGE,e); return IH_FAULT; }
    unsigned status=packed&255, pitch=(packed>>8)&255, velocity=(packed>>16)&255;
    if((status&0xf0)!=0x80 && (status&0xf0)!=0x90)
        return message(s,IH_KEYBOARD,packed,IH_OMNI,e);
    if(pitch>127 || velocity>127) { lost(s,1u<<IH_KEYBOARD,IH_BAD_MESSAGE,e); return IH_FAULT; }
    unsigned down=(status&0xf0)==0x90 && velocity!=0;
    uint16_t tuple=(uint16_t)(((status&15)<<7)+pitch+1);
    uint16_t owner=s->scanner_key_owner[scanner_key];
    if(down && owner) {
        lost(s,1u<<IH_KEYBOARD,owner==tuple?IH_REPEATED_ON:IH_SCANNER_MISMATCH,e);
        return IH_FAULT;
    }
    if(!down && owner!=tuple) {
        lost(s,1u<<IH_KEYBOARD,IH_SCANNER_MISMATCH,e); return IH_FAULT;
    }
    IhResult result=message(s,IH_KEYBOARD,packed,IH_OMNI,e);
    if(result==IH_APPLIED) s->scanner_key_owner[scanner_key]=down?tuple:0;
    else if(result==IH_IGNORED) {
        lost(s,1u<<IH_KEYBOARD,IH_SCANNER_MISMATCH,e); return IH_FAULT;
    }
    return result;
}
IhResult ih_receive_hook(IhState *s,unsigned source,uint32_t packed,unsigned filter,unsigned scanner_key,IhEffects *e) {
    if(source==IH_KEYBOARD && scanner_key!=UINT32_MAX)
        return ih_receive_scanner(s,packed,scanner_key,e);
    if(scanner_key!=UINT32_MAX) {
        if(!s || !e) return IH_INVALID;
        start_effects(s,e); lost(s,IH_ALL_SOURCES,IH_BAD_SOURCE,e); return IH_FAULT;
    }
    return ih_receive_midi(s,source,packed,filter,e);
}
void ih_gesture_notes(const IhState *s,GenGestureContext *c) {
    c->known&=(uint16_t)~GG_K_NOTES;
    c->notes_held=s->held_count!=0;
    if(s->known_sources==IH_ALL_SOURCES) c->known|=GG_K_NOTES;
}
unsigned ih_known_idle(const IhState *s) {
    return s->known_sources==IH_ALL_SOURCES && !s->held_count;
}

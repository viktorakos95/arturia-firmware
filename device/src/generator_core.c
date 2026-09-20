#include "generator_core.h"

/* V4 event kernel. No heap, floating point, device addresses or IRQ effects.
 * The cells are one future-readable block; all event phases refer to its start.
 * Musical usefulness still requires listening on the actual instrument. */
static void zero(void *ptr,unsigned n) {uint8_t *p=ptr;for(unsigned i=0;i<n;++i)p[i]=0;}
static void copy(void *to,const void *from,unsigned n) {
    uint8_t *d=to;const uint8_t *s=from;for(unsigned i=0;i<n;++i)d[i]=s[i];
}
static uint32_t random_next(uint32_t x) {return x*1664525u+1013904223u;}
static uint32_t mix(uint32_t x) {
    x=(x^(x>>16))*0x7feb352du;x=(x^(x>>15))*0x846ca68bu;return x^(x>>16);
}
static unsigned pick(GenSnapshot *s,unsigned n) {s->rng=random_next(s->rng);return (s->rng>>8)%n;}
static uint64_t bit(unsigned i) {return UINT64_C(1)<<i;}
static uint64_t valid_bits(unsigned L) {return L==64?UINT64_MAX:bit(L)-1;}
static unsigned wrap(int i,unsigned L) {int r=i%(int)L;return (unsigned)(r<0?r+(int)L:r);}
static int clamp(int n,int lo,int hi) {return n<lo?lo:n>hi?hi:n;}
static unsigned attacks(const GenSnapshot *s) {
    unsigned n=0;for(unsigned i=0;i<s->params.length;++i)if(s->active&bit(i))++n;return n;
}
static int params_valid(GenParams *p) {
    if(!p->length || p->length>64 || p->density>64 || p->rotation>63 ||
       !p->motion || p->motion>32 || p->evolve>99 || p->accent>99 ||
       !p->accent_period || p->accent_period>16 || p->range>3 || p->mode>7 ||
       (p->auto_period!=0 && p->auto_period!=1 && p->auto_period!=2 &&
        p->auto_period!=4 && p->auto_period!=8))return 0;
    if(p->density>p->length)p->density=p->length;
    p->rotation%=p->length;return 1;
}
static int notes_valid(const uint8_t *notes,unsigned count) {
    if(!notes || !count || count>GEN_NOTES)return 0;
    for(unsigned i=0;i<count;++i)if(notes[i]>127)return 0;return 1;
}
static unsigned sorted_notes(uint8_t *out,const uint8_t *notes,unsigned count) {
    unsigned n=0;
    for(unsigned i=0;i<count;++i) {
        unsigned at=0;while(at<n && out[at]<notes[i])++at;
        if(at<n && out[at]==notes[i])continue;
        for(unsigned j=n;j>at;--j)out[j]=out[j-1];out[at]=notes[i];++n;
    }
    return n;
}
uint64_t gen_euclid_length(unsigned L,unsigned density,unsigned rotation) {
    if(!L || L>64 || density>L || rotation>=L)return 0;
    unsigned remainder=0;uint64_t result=0;
    for(unsigned i=0;i<L;++i) {
        if(remainder<density)result|=bit((i+rotation)%L);
        remainder=(remainder+density)%L;
    }
    return result;
}
uint16_t gen_euclid(unsigned density,unsigned rotation) {
    return (uint16_t)gen_euclid_length(16,density,rotation);
}
static unsigned pitch_pool(const GenSnapshot *s,uint8_t pool[128]) {
    uint32_t seen[4]={0,0,0,0};unsigned n=0;
    for(unsigned i=0;i<s->count;++i)for(unsigned oct=0;oct<=s->params.range;++oct) {
        unsigned p=s->notes[i]+12*oct;if(p<=127)seen[p>>5]|=1u<<(p&31);
    }
    for(unsigned p=0;p<128;++p)if(seen[p>>5]&(1u<<(p&31)))pool[n++]=(uint8_t)p;
    return n;
}
static unsigned nearest(const uint8_t *pool,unsigned n,unsigned pitch) {
    unsigned best=0,dist=255;
    for(unsigned i=0;i<n;++i) {
        unsigned d=pool[i]>pitch?pool[i]-pitch:pitch-pool[i];
        if(d<dist) {dist=d;best=i;}
    }
    return best;
}
static void render(GenSnapshot *s) {
    unsigned ordinal=0;s->known=valid_bits(s->params.length);zero(s->cells,sizeof(s->cells));
    for(unsigned i=0;i<s->params.length;++i) {
        unsigned m=wrap((int)s->motif_phase+s->motif_direction*(int)ordinal,s->params.motion);
        unsigned a=wrap((int)s->accent_phase+s->accent_direction*(int)ordinal,s->params.accent_period);
        s->cells[i].pitch=s->motif[m];
        s->cells[i].velocity=(uint8_t)clamp(96+(int)s->accent_cycle[a]*s->params.accent*31/9801,1,127);
        if(s->active&bit(i))++ordinal;
    }
}
static void fresh(GenSnapshot *s,const uint8_t *notes,unsigned count,GenParams p,uint32_t rng) {
    zero(s,sizeof(*s));s->params=p;s->count=(uint8_t)sorted_notes(s->notes,notes,count);
    uint32_t seed=rng;for(unsigned i=0;i<s->count;++i)seed=mix(seed^(s->notes[i]+1u));
    s->composition_seed=seed;s->rng=random_next(seed);
    s->motif_direction=s->accent_direction=s->accumulator_direction=1;
    uint8_t pool[128];unsigned n=pitch_pool(s,pool);
    /* Complete fixed material is generated once; n/b only select cycle lengths.
     * A local walk gives interval continuity, with occasional wider departures.
     * No special first/last note and no mandatory tonal interpretation. */
    unsigned at=pick(s,n);
    for(unsigned i=0;i<GEN_NOTES;++i) {
        if(i) {
            int delta=(int)pick(s,5)-2;
            if(pick(s,8)==0)delta=(int)pick(s,n)-(int)at;
            at=(unsigned)clamp((int)at+delta,0,(int)n-1);
        }
        s->motif[i]=pool[at];
    }
    for(unsigned i=0;i<16;++i)s->accent_cycle[i]=(int8_t)((int)pick(s,199)-99);
    s->active=gen_euclid_length(p.length,p.density,p.rotation);render(s);
}
int gen_equal(const GenSnapshot *a,const GenSnapshot *b) {
    /* All constructors zero the whole snapshot; compare semantic fields only,
     * never structure padding supplied by a compiler or previous assignment. */
    if(a->rng!=b->rng || a->composition_seed!=b->composition_seed || a->active!=b->active ||
       a->known!=b->known || a->count!=b->count || a->auto_counter!=b->auto_counter ||
       a->motif_phase!=b->motif_phase || a->accent_phase!=b->accent_phase ||
       a->motif_direction!=b->motif_direction || a->accent_direction!=b->accent_direction ||
       a->branch_id!=b->branch_id || a->branch_valid!=b->branch_valid ||
       a->branch_strength!=b->branch_strength || a->accumulator!=b->accumulator ||
       a->accumulator_direction!=b->accumulator_direction)return 0;
    const uint8_t *ap=(const uint8_t *)&a->params,*bp=(const uint8_t *)&b->params;
    for(unsigned i=0;i<sizeof(GenParams);++i)if(ap[i]!=bp[i])return 0;
    for(unsigned i=0;i<a->count;++i)if(a->notes[i]!=b->notes[i])return 0;
    for(unsigned i=0;i<GEN_NOTES;++i)
        if(a->motif[i]!=b->motif[i] || a->branch_base[i]!=b->branch_base[i])return 0;
    for(unsigned i=0;i<16;++i)if(a->accent_cycle[i]!=b->accent_cycle[i])return 0;
    for(unsigned i=0;i<GEN_STEPS;++i)
        if(a->cells[i].pitch!=b->cells[i].pitch || a->cells[i].velocity!=b->cells[i].velocity)return 0;
    return 1;
}
int gen_init(GenState *s,const uint8_t *notes,unsigned count,GenParams p,uint32_t rng) {
    if(!s || !params_valid(&p) || !notes_valid(notes,count))return 0;
    uint8_t saved[32];copy(saved,notes,count);zero(s,sizeof(*s));
    fresh(&s->seed,saved,count,p,rng);s->current=s->seed;s->random_seed=rng;return 1;
}
static int prepare(GenState *s) {
    if(s->pending_count==UINT32_MAX)return 0;
    if(!s->pending_count) {
        s->next_current=s->current;s->next_seed=s->seed;
        zero(&s->pending_feedback,sizeof(s->pending_feedback));
        s->pending_suppress_auto=0;s->pending_phase_reset=0;
    }
    ++s->pending_count;return 1;
}
static GenResult action_result(GenAction action,int changed) {
    GenResult r={1,(uint8_t)action,(uint8_t)changed,0,changed?GEN_CHANGED:GEN_UNCHANGED};return r;
}
int gen_params(GenState *s,GenParams v,unsigned mask) {
    if(!s || (mask&~GEN_ALL_PARAMS))return 0;
    GenParams p=s->pending_count?s->next_current.params:s->current.params,old=p;
    if(mask&GEN_DENSITY)p.density=v.density;
    if(mask&GEN_ROTATION)p.rotation=v.rotation;
    if(mask&GEN_MOTION)p.motion=v.motion;
    if(mask&GEN_EVOLVE)p.evolve=v.evolve;
    if(mask&GEN_LENGTH)p.length=v.length;
    if(mask&GEN_ACCENT)p.accent=v.accent;
    if(mask&GEN_RANGE)p.range=v.range;
    if(mask&GEN_AUTO_PERIOD)p.auto_period=v.auto_period;
    if(mask&GEN_MODE)p.mode=v.mode;
    if(mask&GEN_ACCENT_PERIOD)p.accent_period=v.accent_period;
    if(!params_valid(&p) || !prepare(s))return 0;
    GenSnapshot *c=&s->next_current;c->params=p;
    if(p.length!=old.length || p.density!=old.density)
        c->active=gen_euclid_length(p.length,p.density,p.rotation);
    else if(p.rotation!=old.rotation) {
        unsigned amount=wrap((int)p.rotation-old.rotation,p.length);
        if(amount)c->active=((c->active<<amount)|(c->active>>(p.length-amount)))&valid_bits(p.length);
    }
    c->motif_phase%=p.motion;c->accent_phase%=p.accent_period;
    if(p.motion!=old.motion)c->accumulator=0;
    if(p.range!=old.range) {
        uint8_t pool[128];unsigned n=pitch_pool(c,pool);
        for(unsigned i=0;i<GEN_NOTES;++i) {
            c->motif[i]=pool[nearest(pool,n,c->motif[i])];
            if(c->branch_valid)c->branch_base[i]=pool[nearest(pool,n,c->branch_base[i])];
        }
        c->accumulator=0;
    }
    render(c);
    if(p.length!=old.length || p.density!=old.density || p.rotation!=old.rotation ||
       p.motion!=old.motion || p.accent_period!=old.accent_period || p.range!=old.range ||
       p.accent!=old.accent || p.mode!=old.mode)s->pending_suppress_auto=1;
    return 1;
}
static unsigned depth(const GenSnapshot *s,unsigned n) {
    unsigned d=(n*s->params.evolve+98)/99;return d?d:1;
}
static int set_pitch(GenSnapshot *s,unsigned i,uint8_t pitch) {
    if(s->motif[i]==pitch)return 0;s->motif[i]=pitch;return 1;
}
static int local_fragment(GenSnapshot *s,const uint8_t *pool,unsigned np,int answer) {
    unsigned n=s->params.motion,start=answer?(n+1)/2:pick(s,n);
    if(start>=n)return 0;
    unsigned span=answer?n-start:(n+2)/3,budget=depth(s,span),changed=0;
    int direction=pick(s,2)?1:-1;
    for(unsigned k=0;k<budget;++k) {
        unsigned i=(start+k)%n,at=nearest(pool,np,s->motif[i]);
        int target=(int)at+direction;
        if(answer) {
            unsigned source=(n-1-i)%((n+1)/2);
            target=(int)nearest(pool,np,s->motif[source])+direction;
        }
        target=clamp(target,0,(int)np-1);
        changed+=set_pitch(s,i,pool[target]);
    }
    return (int)changed;
}
static int cycles(GenSnapshot *s) {
    unsigned n=s->params.motion,b=s->params.accent_period;int changed=0;
    if(n>1) {
        unsigned next=(s->motif_phase+1+pick(s,depth(s,n-1)))%n;
        changed|=next!=s->motif_phase;s->motif_phase=(uint8_t)next;
    }
    if(b>1) {s->accent_phase=(uint8_t)((s->accent_phase+1)%b);changed=1;}
    if(s->params.evolve>=50 && pick(s,2)) {
        if(n>1)s->motif_direction=(int8_t)-s->motif_direction;
        if(b>1)s->accent_direction=(int8_t)-s->accent_direction;
        changed|=n>1 || b>1;
    }
    return changed;
}
static int contour(GenSnapshot *s,const uint8_t *pool,unsigned np) {
    unsigned n=s->params.motion,lo=np-1,hi=0,changed=0;
    for(unsigned i=0;i<n;++i) {unsigned at=nearest(pool,np,s->motif[i]);if(at<lo)lo=at;if(at>hi)hi=at;}
    if(hi==lo)return local_fragment(s,pool,np,0);
    if(pick(s,2)) {
        /* Retrograde changes the line's direction as one coherent operation. */
        unsigned pairs=depth(s,n/2);
        for(unsigned i=0;i<pairs && i<n/2;++i) {
            uint8_t a=s->motif[i],b=s->motif[n-1-i];
            changed+=a!=b;s->motif[i]=b;s->motif[n-1-i]=a;
        }
    } else {
        /* Inversion around the line's own pitch span, not a tonal cadence. */
        unsigned budget=depth(s,n),start=pick(s,n);
        for(unsigned j=0;j<budget;++j) {
            unsigned i=(start+j)%n,at=nearest(pool,np,s->motif[i]);
            changed+=set_pitch(s,i,pool[lo+hi-at]);
        }
    }
    return (int)changed;
}
static int accumulate(GenSnapshot *s,const uint8_t *pool,unsigned np) {
    if(np<2)return 0;
    /* Move one shared degree offset back and forth over a finite pool. The
     * individual pitches wrap inside that pool, so a motif already spanning
     * the full pool still develops. No pitch escapes the captured material.
     * Returning the offset to zero restores the exact original degree order. */
    int position=clamp(s->accumulator,0,(int)np-1),dir=s->accumulator_direction;
    unsigned room=dir>0?np-1-(unsigned)position:(unsigned)position;
    if(!room) {dir=-dir;room=dir>0?np-1-(unsigned)position:(unsigned)position;}
    unsigned amount=1+s->params.evolve/34;if(amount>room)amount=room;
    for(unsigned i=0;i<s->params.motion;++i) {
        unsigned at=nearest(pool,np,s->motif[i]);
        s->motif[i]=pool[wrap((int)at+dir*(int)amount,np)];
    }
    s->accumulator_direction=(int8_t)dir;
    s->accumulator=(int16_t)(position+dir*(int)amount);return amount!=0;
}
static int branch(GenSnapshot *s,const uint8_t *pool,unsigned np) {
    if(!s->branch_valid) {
        copy(s->branch_base,s->motif,sizeof(s->motif));s->branch_valid=1;
        s->branch_id=0;s->branch_strength=s->params.evolve;
    }
    unsigned id=pick(s,4),changed=0,n=s->params.motion;
    /* Four named routes in one captured family; revisiting a route is exact,
     * independent of how many times it has been visited or current E. */
    for(unsigned i=0;i<GEN_NOTES;++i) {
        uint8_t pitch=s->branch_base[i];
        if(id && i<n) {
            uint32_t h=mix(s->composition_seed+id*0x9e3779b9u+i*0x7f4a7c15u);
            if(h%99<s->branch_strength) {
                int delta=(int)(h>>16)%(2*(1+s->branch_strength/34)+1)-(1+s->branch_strength/34);
                unsigned at=nearest(pool,np,pitch);pitch=pool[clamp((int)at+delta,0,(int)np-1)];
            }
        }
        changed+=set_pitch(s,i,pitch);
    }
    s->branch_id=(uint8_t)id;return (int)changed;
}
static int syncopate(GenSnapshot *s) {
    unsigned L=s->params.length,H=attacks(s),changed=0;
    unsigned budget=depth(s,H),first=pick(s,L);
    int direction=pick(s,2)?1:-1;
    for(unsigned j=0;j<L && budget;++j) {
        unsigned i=(first+j)%L;
        if(!(s->active&bit(i)))continue;
        unsigned to=wrap((int)i+direction*(int)(1+pick(s,1+s->params.evolve/34)),L);
        if(!(s->active&bit(to))) {s->active^=bit(i)|bit(to);--budget;++changed;}
    }
    if(s->params.accent && s->params.accent_period>1) {
        unsigned i=pick(s,s->params.accent_period),j=(i+1)%s->params.accent_period;
        int8_t a=s->accent_cycle[i],b=s->accent_cycle[j];
        if(a!=b) {s->accent_cycle[i]=b;s->accent_cycle[j]=a;++changed;}
    }
    return (int)changed;
}
static GenResult vary(GenSnapshot *s,unsigned manual) {
    GenResult r=action_result(GEN_VARY,0);
    if(!s->params.evolve) {r.reason=GEN_EVOLVE_ZERO;return r;}
    if(!attacks(s)) {r.reason=GEN_EMPTY;return r;}
    /* Automatic E1 is a 1/99 opportunity, with RNG advanced even on a no-op.
     * Explicit Record requests an operation immediately; E controls its depth.
     * Material constraints may still make an attempted operation a no-op. */
    if(!manual && pick(s,99)>=s->params.evolve)return r;
    uint8_t pool[128];unsigned np=pitch_pool(s,pool),mode=s->params.mode;int changed=0;
    if(mode==GEN_WALK)mode=pick(s,7);
    switch(mode) {
    case GEN_MOTIF:changed=local_fragment(s,pool,np,0);break;
    case GEN_CYCLES:changed=cycles(s);break;
    case GEN_ARCH:changed=contour(s,pool,np);break;
    case GEN_ACCUMULATE:changed=accumulate(s,pool,np);break;
    case GEN_ANSWER:changed=local_fragment(s,pool,np,1);break;
    case GEN_BRANCH:changed=branch(s,pool,np);break;
    case GEN_SYNCOPATED:changed=syncopate(s);break;
    }
    if(changed) {
        if(mode!=GEN_BRANCH && mode!=GEN_CYCLES && mode!=GEN_SYNCOPATED)s->branch_valid=0;
        if(mode!=GEN_ACCUMULATE && mode!=GEN_CYCLES && mode!=GEN_SYNCOPATED)s->accumulator=0;
        r.changed=1;r.reason=GEN_CHANGED;r.changed_steps=(uint8_t)(changed>64?64:changed);render(s);
    } else r.reason=GEN_FIXED;
    return r;
}
static void vary_feedback(GenFeedback *f,GenResult r) {
    if(f->last_vary.valid)f->multiple_variations=1;
    f->last_vary=r;if(r.changed)f->changed_vary=r;
}
int gen_command(GenState *s,GenAction action) {
    if(!s || (action!=GEN_VARY && action!=GEN_RESET && action!=GEN_COMMIT))return 0;
    if(action==GEN_RESET)s->pending_count=0;
    if(!prepare(s))return 0;
    if(action==GEN_VARY) {
        GenResult r=vary(&s->next_current,1);if(r.changed)s->next_current.auto_counter=0;
        vary_feedback(&s->pending_feedback,r);s->pending_suppress_auto=1;
    } else if(action==GEN_COMMIT) {
        s->pending_feedback.memory=action_result(action,!gen_equal(&s->next_seed,&s->current));s->next_seed=s->current;
    } else {
        s->pending_feedback.memory=action_result(action,!gen_equal(&s->next_current,&s->seed));
        s->next_current=s->seed;s->pending_suppress_auto=1;s->pending_phase_reset=1;
    }
    return 1;
}
int gen_new_seed(GenState *s,const uint8_t *notes,unsigned count) {
    if(!s || !notes_valid(notes,count) || s->pending_count==UINT32_MAX)return 0;
    uint8_t saved[32];copy(saved,notes,count);if(!prepare(s))return 0;
    /* Reuse next_seed as storage; avoid a full snapshot on the target stack. */
    fresh(&s->next_seed,saved,count,s->next_current.params,s->random_seed);
    s->next_current=s->next_seed;s->pending_feedback.memory=action_result(GEN_NEW_SEED,1);
    s->pending_suppress_auto=1;s->pending_phase_reset=1;return 1;
}
GenResult gen_feedback(const GenFeedback *f) {
    if(f->memory.valid)return f->memory;
    if(f->changed_vary.valid)return f->changed_vary;return f->last_vary;
}
void gen_boundary_ex(GenState *s,unsigned advance) {
    unsigned pending=s->pending_count,suppress=pending && s->pending_suppress_auto,changed=pending!=0;
    int dm=0,da=0;
    if(advance && !(pending && s->pending_phase_reset)) {
        unsigned h=attacks(&s->current);
        dm=(int)wrap((int)s->current.motif_phase+s->current.motif_direction*(int)h,s->current.params.motion)-s->current.motif_phase;
        da=(int)wrap((int)s->current.accent_phase+s->current.accent_direction*(int)h,s->current.params.accent_period)-s->current.accent_phase;
    }
    zero(&s->last_feedback,sizeof(s->last_feedback));
    if(pending) {
        s->current=s->next_current;s->seed=s->next_seed;s->last_feedback=s->pending_feedback;
        s->pending_count=0;s->pending_suppress_auto=0;s->pending_phase_reset=0;
    }
    if(!advance) {if(changed)++s->revision;return;}
    s->current.motif_phase=(uint8_t)wrap((int)s->current.motif_phase+dm,s->current.params.motion);
    s->current.accent_phase=(uint8_t)wrap((int)s->current.accent_phase+da,s->current.params.accent_period);
    changed|=dm!=0 || da!=0;
    render(&s->current);
    if(suppress) {if(changed)++s->revision;return;}
    if(!s->current.params.evolve || !s->current.params.auto_period || !attacks(&s->current)) {
        changed|=s->current.auto_counter!=0;s->current.auto_counter=0;
        if(changed)++s->revision;return;
    }
    changed=1;
    if(++s->current.auto_counter>=s->current.params.auto_period) {
        s->current.auto_counter=0;GenResult r=vary(&s->current,0);
        vary_feedback(&s->last_feedback,r);
    }
    if(changed)++s->revision;
}
void gen_boundary(GenState *s) {gen_boundary_ex(s,0);}
void gen_cancel(GenState *s) {
    s->pending_count=0;s->pending_suppress_auto=0;s->pending_phase_reset=0;
    zero(&s->pending_feedback,sizeof(s->pending_feedback));
}
void gen_control_init(GenControl *c, unsigned value) {
    zero(c, sizeof(*c)); c->value = (uint8_t)(value > 127 ? 127 : value);
}
int gen_control_reset(GenControl *c, unsigned value) {
    if (value > 127) return 0;
    c->value = (uint8_t)value; c->previous_valid = 0; c->latched = 0; return 1;
}
GenScaleReason gen_control_move(GenControl *c, unsigned absolute) {
    if (absolute > 127) return SCALE_INVALID;
    if (c->physical_valid && c->physical == absolute) return SCALE_UNCHANGED;
    int p = (int)absolute, t = c->value, value = t;
    GenScaleReason reason;
    if (c->latched) { value = p; reason = SCALE_LATCHED; }
    else if (p - t >= -1 && p - t <= 1) { c->latched = 1; value = p; reason = SCALE_NEAR; }
    else if (!c->previous_valid) reason = SCALE_BASELINE;
    else {
        int d = p - c->previous, denominator = d > 0 ? 127 - p : p;
        if (!d) reason = SCALE_SAME;
        else if (!denominator) reason = SCALE_ENDPOINT;
        else {
            int remaining = d > 0 ? 127 - t : t;
            int g = t + d * remaining / denominator;
            if (g > 127) g = 127;
            if ((p - g) * (p - t) <= 0) { c->latched = 1; value = p; reason = SCALE_CROSSING; }
            else if (g < 0) { value = 0; reason = SCALE_GUARD; }
            else { value = g; reason = SCALE_SCALED; }
        }
    }
    c->value = (uint8_t)value; c->physical = (uint8_t)p; c->previous = (uint8_t)p;
    c->physical_valid = 1; c->previous_valid = 1;
    return reason;
}

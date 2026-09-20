/* Hardware-independent V4 semantic regression. No JS reference and no MIDI.
 * Invariants below model event time independently of core rendering. */
#include "generator_core.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned long checks,blocks,commands;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);assert(x);}} while(0)
static GenState s,t;
static uint32_t test_rng=0x315cc174;
static unsigned rnd(unsigned n) {test_rng=test_rng*1664525u+1013904223u;return (test_rng>>8)%n;}
static unsigned modulo(int v,unsigned n) {int r=v%(int)n;return (unsigned)(r<0?r+(int)n:r);}
static unsigned pop(uint64_t v) {unsigned n=0;while(v) {n+=(unsigned)v&1u;v>>=1;}return n;}
static GenParams params(void) {GenParams p={5,0,7,0,32,55,0,1,GEN_MOTIF,3};return p;}
static const uint8_t triad[]={60,64,67};
static void init(GenParams p,unsigned seed) {CHECK(gen_init(&s,triad,3,p,seed));}
static void boundary(unsigned advance) {gen_boundary_ex(&s,advance);++blocks;}
static void command(GenAction a) {CHECK(gen_command(&s,a));++commands;}
static void parameter(GenParams p,unsigned mask) {CHECK(gen_params(&s,p,mask));++commands;}
static void check_snapshot(const GenSnapshot *c) {
    unsigned L=c->params.length,n=c->params.motion,b=c->params.accent_period,k=0;
    CHECK(L>=1 && L<=64 && n>=1 && n<=32 && b>=1 && b<=16);
    CHECK(c->motif_phase<n && c->accent_phase<b);
    CHECK(c->motif_direction==1 || c->motif_direction==-1);
    CHECK(c->accent_direction==1 || c->accent_direction==-1);
    CHECK(c->accumulator>=-127 && c->accumulator<=127);
    CHECK(pop(c->active)==c->params.density);
    CHECK(c->known==(L==64?UINT64_MAX:((UINT64_C(1)<<L)-1)));
    CHECK(!(c->active & ~c->known));
    for(unsigned i=0;i<32;++i) {
        int allowed=0;
        for(unsigned j=0;j<c->count;++j)for(unsigned oct=0;oct<=c->params.range;++oct)
            if(c->notes[j]+12*oct==c->motif[i])allowed=1;
        CHECK(allowed);
    }
    for(unsigned i=0;i<L;++i) {
        unsigned mi=modulo(c->motif_phase+c->motif_direction*(int)k,n);
        unsigned ai=modulo(c->accent_phase+c->accent_direction*(int)k,b);
        int v=96+c->accent_cycle[ai]*c->params.accent*31/9801;
        CHECK(c->cells[i].pitch==c->motif[mi]);CHECK(c->cells[i].velocity==v);
        CHECK(c->cells[i].pitch<=127 && c->cells[i].velocity>=1 && c->cells[i].velocity<=127);
        if(c->active&(UINT64_C(1)<<i))++k;
    }
    for(unsigned i=L;i<64;++i)CHECK(c->cells[i].pitch==0 && c->cells[i].velocity==0);
}
static int same_material(const GenSnapshot *a,const GenSnapshot *b) {
    return !memcmp(a->motif,b->motif,32) && !memcmp(a->accent_cycle,b->accent_cycle,16);
}
static void base_matrix(void) {
    for(unsigned L=1;L<=64;++L)for(unsigned d=0;d<=L;++d)for(unsigned mode=0;mode<8;++mode) {
        GenParams p=params();p.length=(uint8_t)L;p.density=(uint8_t)d;p.mode=(uint8_t)mode;
        p.motion=(uint8_t)(1+(L+mode)%32);p.accent_period=(uint8_t)(1+(d+mode)%16);
        p.rotation=(uint8_t)((L*7+mode)%64);p.range=(uint8_t)(mode%4);init(p,L*997+d*31+mode);
        check_snapshot(&s.current);GenSnapshot old=s.current;boundary(0);CHECK(gen_equal(&old,&s.current));
        boundary(1);check_snapshot(&s.current);
        CHECK(s.current.motif_phase==d%p.motion && s.current.accent_phase==d%p.accent_period);
        CHECK(s.current.rng==old.rng && same_material(&old,&s.current));
    }
}
static void phase_contract(void) {
    GenParams p=params();init(p,37);GenSnapshot original=s.current;
    for(unsigned i=1;i<=21;++i) {
        boundary(1);CHECK(s.current.motif_phase==(i*5)%7);CHECK(s.current.accent_phase==(i*5)%3);
        CHECK(s.current.rng==original.rng);CHECK(same_material(&s.current,&original));
        CHECK(i==21 || s.current.motif_phase!=0 || s.current.accent_phase!=0);check_snapshot(&s.current);
    }
    CHECK(gen_equal(&s.current,&original));
    /* An edit projects the next block, but finishes OLD d/OLD direction first. */
    for(unsigned oldn=1;oldn<=32;++oldn)for(unsigned newn=1;newn<=32;++newn)
      for(unsigned d=0;d<=64;d+=8) {
        p=params();p.length=64;p.motion=(uint8_t)oldn;p.density=(uint8_t)d;init(p,37);boundary(1);
        unsigned old_phase=s.current.motif_phase,old_a=s.current.accent_phase;
        unsigned end=modulo((int)old_phase+(int)d,oldn),aend=(old_a+d)%3;
        p.motion=(uint8_t)newn;p.density=(uint8_t)((d+11)%65);parameter(p,GEN_MOTION|GEN_DENSITY);
        uint32_t rev=s.revision;boundary(1);
        CHECK(s.current.motif_phase==end%newn);CHECK(s.current.accent_phase==aend);
        CHECK(s.revision==rev+1);check_snapshot(&s.current);
    }
    /* CYC changes future reading direction but cannot retroactively reverse
     * the already played block. Exercise both reverse/forward projections. */
    unsigned reversed=0;
    for(unsigned seed=0;seed<256;++seed) {
        p=params();p.evolve=99;p.mode=GEN_CYCLES;init(p,seed);s.current.auto_counter=0;
        command(GEN_VARY);GenSnapshot projected=s.next_current,old=s.current;
        int dm=(int)modulo(old.motif_phase+old.motif_direction*5,7)-old.motif_phase;
        int da=(int)modulo(old.accent_phase+old.accent_direction*5,3)-old.accent_phase;
        if(projected.motif_direction<0)++reversed;
        boundary(1);
        CHECK(s.current.motif_phase==modulo(projected.motif_phase+dm,7));
        CHECK(s.current.accent_phase==modulo(projected.accent_phase+da,3));
        CHECK(s.current.motif_direction==projected.motif_direction);
        CHECK(s.current.rng==projected.rng);CHECK(same_material(&old,&s.current));check_snapshot(&s.current);
    }
    CHECK(reversed>50);
}
static void memory_contract(void) {
    GenParams p=params();p.evolve=75;p.mode=GEN_WALK;init(p,97);
    for(unsigned i=0;i<9;++i)boundary(1);
    GenSnapshot heard=s.current;uint32_t oldrev=s.revision;
    p.density=2;parameter(p,GEN_DENSITY);command(GEN_VARY);command(GEN_COMMIT);
    CHECK(gen_equal(&s.next_seed,&heard));CHECK(gen_equal(&s.current,&heard));boundary(1);
    CHECK(s.revision==oldrev+1);CHECK(gen_equal(&s.seed,&heard));
    command(GEN_VARY);p.motion=3;parameter(p,GEN_MOTION);command(GEN_RESET);
    CHECK(s.pending_count==1);boundary(1);CHECK(gen_equal(&s.current,&heard));
    /* Reset replays RNG and phase: deterministic subsequent requests. */
    t=s;for(unsigned i=0;i<32;++i) {command(GEN_VARY);boundary(1);}
    GenSnapshot after=s.current;s=t;command(GEN_RESET);boundary(1);
    for(unsigned i=0;i<32;++i) {command(GEN_VARY);boundary(1);}
    CHECK(gen_equal(&s.current,&after));
    /* New capture is not advanced by the old five-event block. */
    const uint8_t input[]={55,59,62,65};CHECK(gen_new_seed(&s,input,4));
    GenSnapshot captured=s.next_current;boundary(1);CHECK(gen_equal(&captured,&s.current));
    CHECK(gen_equal(&captured,&s.seed));
    /* Commit then Reset before publication still restores the APPLIED seed. */
    heard=s.seed;boundary(1);command(GEN_COMMIT);command(GEN_RESET);boundary(1);
    CHECK(gen_equal(&s.current,&heard));CHECK(gen_equal(&s.seed,&heard));
    /* Stop/PRE/cancel are never a musical tick. */
    heard=s.current;parameter(params(),GEN_DENSITY);gen_cancel(&s);boundary(0);
    CHECK(gen_equal(&s.current,&heard));
    /* Source aliasing survives state reset and projection initialization. */
    p=params();CHECK(gen_init(&s,s.current.notes,s.current.count,p,92));
    CHECK(s.current.count==4 && s.current.notes[0]==55);
    CHECK(gen_new_seed(&s,s.current.notes,s.current.count));boundary(0);CHECK(s.current.notes[3]==65);
}
static void invariants_and_operator_semantics(void) {
    GenParams p=params();init(p,3);GenSnapshot old=s.current;
    for(unsigned mode=0;mode<8;++mode) {
        p.mode=(uint8_t)mode;parameter(p,GEN_MODE);boundary(0);
        CHECK(same_material(&old,&s.current));CHECK(s.current.rng==old.rng);
        CHECK(!memcmp(old.cells,s.current.cells,sizeof(old.cells)));
    }
    p=params();p.evolve=99;p.density=0;init(p,89);old=s.current;
    for(unsigned i=0;i<1000;++i) {boundary(1);command(GEN_VARY);boundary(0);}
    CHECK(gen_equal(&old,&s.current));
    p=params();p.evolve=1;init(p,98);unsigned skips=0,changed=0;
    for(unsigned i=0;i<1000;++i) {
        old=s.current;boundary(1);
        CHECK(s.current.rng!=old.rng);if(s.last_feedback.last_vary.changed)++changed;else ++skips;
    }
    CHECK(skips>950 && changed>0 && changed<30);
    p=params();p.evolve=1;p.mode=GEN_CYCLES;init(p,98);old=s.current;
    command(GEN_VARY);boundary(0);CHECK(s.last_feedback.last_vary.changed);
    CHECK(s.current.motif_phase!=old.motif_phase && same_material(&old,&s.current));
    /* Parameter edits preserve event material and do not consume randomness. */
    p=params();init(p,13);old=s.current;
    for(unsigned d=0;d<=32;++d) {
        p.density=(uint8_t)d;parameter(p,GEN_DENSITY);boundary(0);
        CHECK(same_material(&old,&s.current));CHECK(s.current.rng==old.rng);check_snapshot(&s.current);
    }
    p=params();p.mode=GEN_ANSWER;p.evolve=99;init(p,66);old=s.current;
    for(unsigned k=0;k<32;++k) {command(GEN_VARY);boundary(0);CHECK(!memcmp(old.motif,s.current.motif,4));}
    p=params();p.mode=GEN_SYNCOPATED;p.evolve=99;init(p,72);old=s.current;
    unsigned rhythm_changes=0;
    for(unsigned k=0;k<32;++k) {command(GEN_VARY);boundary(0);CHECK(!memcmp(old.motif,s.current.motif,32));rhythm_changes+=old.active!=s.current.active;check_snapshot(&s.current);}
    CHECK(rhythm_changes>10);
    /* Rotation moves the currently heard mutated rhythm; it does not replace
     * that rhythm with a fresh Euclidean distribution. */
    for(unsigned L=1;L<=64;++L) {
        p=params();p.length=(uint8_t)L;p.density=(uint8_t)(L/3);p.evolve=99;p.mode=GEN_SYNCOPATED;
        init(p,L);command(GEN_VARY);boundary(0);old=s.current;
        for(unsigned amount=0;amount<L;++amount) {
            p.rotation=(uint8_t)amount;parameter(p,GEN_ROTATION);boundary(0);uint64_t expected=0;
            for(unsigned i=0;i<L;++i)if(old.active&(UINT64_C(1)<<i))expected|=UINT64_C(1)<<((i+amount)%L);
            CHECK(s.current.active==expected);CHECK(same_material(&old,&s.current));CHECK(s.current.rng==old.rng);
        }
    }
    /* Stable branch identities really return the same content, even after E
     * changes the probability of selection. The family captures its depth. */
    p=params();p.mode=GEN_BRANCH;p.evolve=99;init(p,99);
    uint8_t known[4]={0},routes[4][32];unsigned visits[4]={0};
    for(unsigned i=0;i<160;++i) {
        command(GEN_VARY);boundary(0);unsigned id=s.current.branch_id;
        if(known[id])CHECK(!memcmp(routes[id],s.current.motif,32));
        else {known[id]=1;memcpy(routes[id],s.current.motif,32);}++visits[id];
    }
    for(unsigned i=0;i<4;++i)CHECK(visits[i]>20);
    p.evolve=80;parameter(p,GEN_EVOLVE);boundary(0);
    for(unsigned i=0;i<160;++i) {command(GEN_VARY);boundary(0);CHECK(!memcmp(routes[s.current.branch_id],s.current.motif,32));}
    /* Accumulator reaches limits and returns without growing out of pool. */
    p=params();p.motion=1;p.range=3;p.evolve=99;p.mode=GEN_ACCUMULATE;init(p,40);
    unsigned up=0,down=0;
    for(unsigned i=0;i<2048;++i) {
        old=s.current;command(GEN_VARY);boundary(0);up+=s.current.motif[0]>old.motif[0];
        down+=s.current.motif[0]<old.motif[0];check_snapshot(&s.current);
    }
    CHECK(up>100 && down>100);
    /* A motif that spans every permitted pitch must not disable ACC. A whole
     * bounded offset excursion returns the precise motif, including repeats. */
    p=params();p.evolve=99;p.mode=GEN_ACCUMULATE;init(p,14);
    for(unsigned i=0;i<32;++i)s.current.motif[i]=triad[i%3];
    old=s.current;command(GEN_VARY);boundary(0);
    CHECK(memcmp(old.motif,s.current.motif,p.motion)!=0);CHECK(s.current.accumulator==2);
    command(GEN_VARY);boundary(0);CHECK(!memcmp(old.motif,s.current.motif,32));
    CHECK(s.current.accumulator==0);
    /* Manual variation and explicit material edit suppress a second automatic
     * action on the same boundary; E/t-only edits do not reset material. */
    p=params();p.evolve=99;init(p,913);command(GEN_VARY);old=s.next_current;
    boundary(1);CHECK(s.current.rng==old.rng);CHECK(!s.last_feedback.multiple_variations);
    p.density=4;parameter(p,GEN_DENSITY);old=s.next_current;boundary(1);CHECK(s.current.rng==old.rng);
}
static void stress(void) {
    GenParams p=params();uint8_t many[32];for(unsigned i=0;i<32;++i)many[i]=(uint8_t)(i*4);
    for(unsigned session=0;session<32;++session) {
        p=params();p.range=3;p.motion=32;p.evolve=99;p.accent_period=16;
        CHECK(gen_init(&s,many,32,p,session));
        for(unsigned j=0;j<4000;++j) {
            unsigned op=rnd(20);
            if(op<10) {p=s.current.params;p.mode=(uint8_t)rnd(8);parameter(p,GEN_MODE);command(GEN_VARY);boundary(rnd(2));}
            else if(op<14)boundary(1);
            else if(op==14) {command(GEN_COMMIT);boundary(1);}
            else if(op==15) {command(GEN_RESET);boundary(1);}
            else {
                p=s.current.params;p.length=(uint8_t)(1+rnd(64));p.density=(uint8_t)rnd(65);
                p.motion=(uint8_t)(1+rnd(32));p.accent_period=(uint8_t)(1+rnd(16));
                p.range=(uint8_t)rnd(4);p.rotation=(uint8_t)rnd(64);p.evolve=(uint8_t)rnd(100);
                parameter(p,GEN_ALL_PARAMS);boundary(rnd(2));
            }
            check_snapshot(&s.current);CHECK(!s.pending_count);
        }
    }
    /* Every input pitch / octave clamp, plus duplicate capture. */
    for(unsigned pitch=0;pitch<128;++pitch)for(unsigned range=0;range<4;++range) {
        uint8_t input[3]={(uint8_t)pitch,(uint8_t)pitch,(uint8_t)pitch};
        p=params();p.range=(uint8_t)range;p.evolve=99;
        CHECK(gen_init(&s,input,3,p,pitch));CHECK(s.current.count==1);
        for(unsigned mode=0;mode<8;++mode) {p.mode=(uint8_t)mode;parameter(p,GEN_MODE);command(GEN_VARY);boundary(1);check_snapshot(&s.current);}
    }
}
static void negative(void) {
    GenParams p=params();init(p,123);t=s;GenParams bad=p;
    bad.motion=0;CHECK(!gen_params(&s,bad,GEN_MOTION));CHECK(!memcmp(&s,&t,sizeof(s)));
    bad.motion=33;CHECK(!gen_params(&s,bad,GEN_MOTION));CHECK(!memcmp(&s,&t,sizeof(s)));
    bad=p;bad.accent_period=0;CHECK(!gen_params(&s,bad,GEN_ACCENT_PERIOD));
    bad.accent_period=17;CHECK(!gen_params(&s,bad,GEN_ACCENT_PERIOD));CHECK(!memcmp(&s,&t,sizeof(s)));
    bad=p;bad.length=0;CHECK(!gen_params(&s,bad,GEN_LENGTH));CHECK(!memcmp(&s,&t,sizeof(s)));
    bad=p;bad.auto_period=3;CHECK(!gen_params(&s,bad,GEN_AUTO_PERIOD));CHECK(!memcmp(&s,&t,sizeof(s)));
    CHECK(!gen_params(&s,p,1024));CHECK(!gen_new_seed(&s,NULL,3));
    uint8_t invalid[]={128};CHECK(!gen_new_seed(&s,invalid,1));CHECK(!memcmp(&s,&t,sizeof(s)));
    s.pending_count=UINT32_MAX;t=s;
    CHECK(!gen_command(&s,GEN_VARY));CHECK(!gen_params(&s,p,GEN_MODE));CHECK(!gen_new_seed(&s,triad,3));
    CHECK(!memcmp(&s,&t,sizeof(s)));command(GEN_RESET);CHECK(s.pending_count==1);boundary(1);
    CHECK(gen_euclid_length(0,0,0)==0 && gen_euclid_length(65,1,0)==0);
    CHECK(gen_euclid_length(64,64,0)==UINT64_MAX);
}
int main(void) {
    base_matrix();phase_contract();memory_contract();invariants_and_operator_semantics();stress();negative();
    printf("{\"status\":\"PASS\",\"assertions\":%lu,\"blocks\":%lu,\"commands\":%lu,"
           "\"base_states\":17152,\"stress_events\":128000,\"layout\":{\"params\":%zu,\"snapshot\":%zu,\"state\":%zu,\"cells_offset\":%zu,\"control\":%zu}}\n",
           checks,blocks,commands,sizeof(GenParams),sizeof(GenSnapshot),sizeof(GenState),offsetof(GenSnapshot,cells),sizeof(GenControl));
    return 0;
}

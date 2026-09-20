/* Hardware-independent controller, Scale, panel and LED projection tests. */
#include "generator_controller.h"
#include "generator_panel.h"
#include "generator_keyled.h"
#include "generator_rate12.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define C(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 0;} }while(0)
static const GenParams defaults={5,0,7,0,32,55,0,1,0,3};
static const uint8_t notes[]={48,51,55,58};
static GenGestureContext ctx={.known=GG_K_ALL,.seq=1,.page=GG_PAGE_CHORD,.transport=GG_STOPPED,.record_idle=1};
static int init(GenController*s,GenParams p){C(gen_controller_init(s,notes,4,p,3701));s->gesture.active=1;return 1;}
static unsigned expected(unsigned logical,unsigned u,unsigned L){
 const unsigned periods[]={0,1,2,4,8};
 return logical==GCP_LENGTH?1+u*63/127:logical==GCP_DENSITY?u*L/127:
 logical==GCP_ROTATION?u*(L-1)/127:logical==GCP_MOTION?1+u*31/127:logical==GCP_ACCENT_PERIOD?1+u*15/127:
 logical==GCP_AUTO?periods[u*4/127]:u*99/127;
}
static unsigned unit(unsigned value,unsigned max){return max?(value*127+max-1)/max:0;}
static int mappings(void){
 C(sizeof(GenControl)==6);
 for(unsigned p=0;p<8;p++)for(unsigned u=0;u<128;u++){
  GenController s;GenControllerEffects e;C(init(&s,defaults));
  s.gesture.held=p>=4?1u<<GG_SHIFT:0;s.knobs[p].latched=1;
  C(gen_controller_knob(&s,p%4,u,&ctx,&e));
  C(e.flags&GC_PARAMETER);C(e.parameter==p);C(e.value==expected(p,u,32));
  C(gen_controller_parameter_value(&s,p)==e.value);
  C(s.knobs[p%4].physical==u&&s.knobs[p%4+4].physical==u);
 }
 for(unsigned L=1;L<=64;L++)for(unsigned d=0;d<=L;d++){
  GenParams p=defaults;p.length=L;p.density=d;p.rotation=L-1;
  GenController s;C(init(&s,p));
  C(expected(GCP_LENGTH,s.knobs[0].value,L)==L);
  C(expected(GCP_DENSITY,s.knobs[1].value,L)==d);
  C(expected(GCP_ROTATION,s.knobs[2].value,L)==L-1);
 }
 /* Unchanged Scale algorithm, including rounding plateaus, on every page. */
 for(unsigned p=0;p<8;p++){
  GenController s;GenControllerEffects e;C(init(&s,defaults));
  s.gesture.held=p>=4?1u<<GG_SHIFT:0;GenControl reference=s.knobs[p];
  unsigned rng=3701+p;
  for(unsigned n=0;n<1024;n++){
   rng=rng*1664525u+1013904223u;unsigned v=(rng>>24)&127;
   gen_control_move(&reference,v);C(gen_controller_knob(&s,p%4,v,&ctx,&e));
   C(!memcmp(&reference,&s.knobs[p],sizeof(reference)));
  }
 }
 return 1;
}
static int pages(void){
 GenController s;GenControllerEffects e;C(init(&s,defaults));
 /* OFF observes the real physical knob for both independent targets. */
 s.gesture.active=0;GenState original=s.core;
 C(gen_controller_knob(&s,0,100,&ctx,&e));C(e.flags==GC_NATIVE_KNOB);
 C(!memcmp(&original,&s.core,sizeof(original)));
 C(s.knobs[0].physical==100&&s.knobs[4].physical==100);
 s.gesture.active=1;
 C(gen_controller_button(&s,GG_SHIFT,1,&ctx,&e));
 C(!s.knobs[0].previous_valid&&!s.knobs[4].previous_valid);
 C(gen_controller_knob(&s,0,100,&ctx,&e));C(!e.flags);
 C(gen_controller_knob(&s,0,101,&ctx,&e));C(e.parameter==GCP_MOTION);
 C(e.scale_reason==SCALE_BASELINE&&e.value==7);
 C(s.knobs[0].physical==101&&s.knobs[4].physical==101);
 C(gen_controller_button(&s,GG_SHIFT,0,&ctx,&e));
 C(gen_controller_knob(&s,0,101,&ctx,&e));C(!e.flags);
 C(gen_controller_knob(&s,0,102,&ctx,&e));C(e.parameter==GCP_LENGTH&&e.value==32);
 /* Length retains attack count, then clamps/mods and retargets d/r Scale. */
 GenParams p=defaults;p.length=64;p.density=50;p.rotation=60;C(init(&s,p));
 s.knobs[0].latched=1;C(gen_controller_knob(&s,0,32,&ctx,&e));
 p=gen_controller_parameters(&s);C(p.length==16&&p.density==16&&p.rotation==12);
 C(s.knobs[1].value==127&&s.knobs[2].value==unit(12,15));
 C(!s.knobs[1].previous_valid&&!s.knobs[2].latched);
 C(gen_controller_knob(&s,0,127,&ctx,&e));p=gen_controller_parameters(&s);
 C(p.length==64&&p.density==16&&p.rotation==12);
 C(s.knobs[1].value==unit(16,64)&&s.knobs[2].value==unit(12,63));
 /* Rejection rolls back target, keeping the physical edge and rearm. */
 C(init(&s,defaults));s.core.pending_count=UINT32_MAX;s.core.next_current=s.core.current;
 unsigned old=s.knobs[3].value;s.knobs[3].latched=1;
 C(gen_controller_knob(&s,3,127,&ctx,&e));C(e.flags&GC_REJECTED);
 C(e.value==0&&s.knobs[3].value==old&&s.knobs[3].physical==127&&!s.knobs[3].latched);
 return 1;
}
static int commands(void){
 GenController s;GenControllerEffects e;GenParams p=defaults;p.evolve=80;p.density=12;
 C(init(&s,p));GenSnapshot heard=s.core.current;
 C(gen_command(&s.core,GEN_VARY));
 C(gen_controller_button(&s,GG_SHIFT,1,&ctx,&e));
 C(gen_controller_button(&s,GG_RECORD,1,&ctx,&e));
 C(gen_controller_button(&s,GG_SHIFT,0,&ctx,&e));
 C(gen_controller_button(&s,GG_RECORD,0,&ctx,&e));
 C(e.flags&GC_QUEUED);C(gen_equal(&s.core.next_seed,&heard));
 /* Reset discards earlier queue, restores all eight targets, remembers Mode. */
 p=gen_controller_parameters(&s);p.length=17;p.density=17;p.rotation=16;p.motion=32;p.accent_period=16;p.accent=0;p.range=3;p.auto_period=8;p.mode=7;
 C(gen_params(&s.core,p,GEN_ALL_PARAMS));
 C(gen_controller_button(&s,GG_SHIFT,1,&ctx,&e));
 C(gen_controller_button(&s,GG_STOP,1,&ctx,&e));
 C(gen_controller_button(&s,GG_STOP,0,&ctx,&e));
 C(gen_controller_button(&s,GG_SHIFT,0,&ctx,&e));
 C(gen_equal(&s.core.next_current,&s.core.seed)&&s.pending_restore);
 C(gen_controller_parameter_value(&s,GCP_LENGTH)==32);
 GenState final=s.core;gen_boundary(&final);s.core=final;
 gen_controller_publication_applied(&s,&e);
 C(e.flags==GC_APPLIED&&!s.pending_restore&&!memcmp(&s.core,&final,sizeof(final)));
 for(unsigned j=0;j<8;j++)C(expected(j,s.knobs[j].value,32)==gen_controller_parameter_value(&s,j));
 /* Established Chord capture still owns notes and release, not Shift page. */
 C(init(&s,defaults));C(gen_controller_button(&s,GG_CHORD,1,&ctx,&e));
 C(gen_controller_note(&s,67,1,&ctx,&e));C(gen_controller_note(&s,60,1,&ctx,&e));
 C(s.capture_count==2&&s.capture[0]==60&&s.capture[1]==67);
 C(gen_controller_button(&s,GG_CHORD,0,&ctx,&e));
 C((e.flags&GC_CAPTURE_COMPLETE)&&e.count==2&&!s.capture_count);
 C(s.core.next_current.count==2&&s.core.next_seed.notes[0]==60);
 return 1;
}
static int modes(void){
 for(unsigned from=0;from<8;from++)for(unsigned to=0;to<8;to++){
  GenController s;GenControllerEffects e;GenParams p=defaults;p.mode=from;C(init(&s,p));
  C(gen_controller_mode_observe(&s,from,&ctx,&e));C(!e.flags);
  C(gen_controller_mode_observe(&s,to,&ctx,&e));
  C(gen_controller_parameters(&s).mode==to);
  unsigned pending=s.core.pending_count;
  C(gen_controller_mode_observe(&s,to,&ctx,&e));C(!e.flags&&s.core.pending_count==pending);
  C(gen_controller_mode_observe(&s,8,&ctx,&e));C(!e.flags&&s.core.pending_count==pending);
  C(gen_controller_mode_observe(&s,to,&ctx,&e));C(s.core.pending_count==pending);
  C(gen_controller_mode_observe(&s,from,&ctx,&e));C(gen_controller_parameters(&s).mode==from);
  C(!gen_controller_mode_observe(&s,9,&ctx,&e));
 }
 GenController s;GenControllerEffects e;C(init(&s,defaults));s.gesture.active=0;
 C(gen_controller_mode_observe(&s,0,&ctx,&e));C(gen_controller_mode_observe(&s,7,&ctx,&e));
 C(!e.flags&&!s.core.pending_count&&s.core.current.params.mode==0);
 s.gesture.active=1;C(gen_controller_mode_observe(&s,7,&ctx,&e));C(!e.flags&&!s.core.pending_count);
 C(gen_controller_button(&s,GG_CHORD,1,&ctx,&e));
 C(gen_controller_mode_observe(&s,6,&ctx,&e));
 C(s.gesture.owners[GG_CHORD].flags&GG_CANCELLED);
 C(e.parameter==GCP_MODE&&e.value==6&&(e.flags&GC_QUEUED));
 return 1;
}
static int panel(void){
 GenPanelState s={0};GenPanelInput i={.active=1,.step=31,.page_valid=1};GenPanelView v;
 const char *labels[]={"L64","d32","r31","E99","n32","A99","b16","t08"};
 const uint8_t values[]={64,32,31,99,32,99,16,8};memcpy(i.values,values,sizeof(values));
 gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"GEn",3));
 for(unsigned n=0;n<8;n++){
  i.now=i.event_tick=100+n;i.flags=GC_PARAMETER;i.parameter=n;
  gp_update(&s,&i);gp_view_rate12(&s,&i,&v,188);C(!memcmp(v.text,labels[n],3));
  C(v.glyphs[0]&&v.glyphs[1]&&v.glyphs[2]);
 }
 const char *names[]={"rPt","CYC","ArC","ACC","AnS","brn","SYn","rnd"};
 for(unsigned n=0;n<8;n++){
  i.now=i.event_tick=200+n;i.parameter=GCP_MODE;i.mode=n;
  gp_update(&s,&i);gp_view_rate12(&s,&i,&v,188);C(!memcmp(v.text,names[n],3));
  for(unsigned j=0;j<3;j++)C(v.glyphs[j]);
 }
 i.flags=0;i.now=300;gp_rate_notice12(&s,300);gp_update(&s,&i);gp_view_rate12(&s,&i,&v,188);C(!memcmp(v.text,"188",3));
 i.capturing=1;i.capture_count=2;gp_update(&s,&i);gp_view_rate12(&s,&i,&v,188);C(!memcmp(v.text,"002",3));
 i.fault=54;gp_view_rate12(&s,&i,&v,188);C(!memcmp(v.text,"F54",3)&&v.color==42);
 memset(&s,0,sizeof(s));i=(GenPanelInput){.active=1,.page_valid=1};gp_update(&s,&i);
 i.now=2000;i.page=1;gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"rPt",3));
 i.now=2399;gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"rPt",3));
 i.now=2400;gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"rPt",3));
 gp_rate_notice12(&s,2500);i.now=2600;i.page=2;gp_update(&s,&i);gp_view_rate12(&s,&i,&v,120);C(!memcmp(v.text,"120",3));
 i.now=4500;gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"rPt",3));
 i.now=i.event_tick=5000;i.flags=GC_PARAMETER;i.parameter=GCP_RANGE;i.range=3;
 gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"O03",3));
 i.flags=0;
 /* Wrapped clocks preserve bounded RATE, page and command lifetimes. */
 gp_rate_notice12(&s,UINT32_MAX-99);i.now=1899;gp_update(&s,&i);gp_view_rate12(&s,&i,&v,240);C(!memcmp(v.text,"240",3));
 i.now=1900;gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"rPt",3));
 for(unsigned step=0;step<64;step++){i.step=step;gp_update(&s,&i);gp_view(&s,&i,&v);C(!memcmp(v.text,"rPt",3));}
 return 1;
}
static int octave(void){
 GenController s;GenControllerEffects e;C(init(&s,defaults));
 C(gen_controller_octave(&s,1,1,&ctx,&e)==GC_OCTAVE_NATIVE);C(!e.flags);
 C(gen_controller_button(&s,GG_SHIFT,1,&ctx,&e));
 C(gen_controller_octave(&s,1,0,&ctx,&e)==GC_OCTAVE_NATIVE);C(!s.octave_held);
 for(unsigned v=1;v<=4;v++){
  C(gen_controller_octave(&s,1,1,&ctx,&e)==GC_OCTAVE_OWNED);
  C((e.flags&GC_PARAMETER)&&e.parameter==GCP_RANGE&&e.value==(v>3?3:v));
  C(!!(e.flags&GC_QUEUED)==(v<=3));unsigned pending=s.core.pending_count;
  C(gen_controller_octave(&s,1,1,&ctx,&e)==GC_OCTAVE_OWNED);C(!e.flags&&s.core.pending_count==pending);
  C(gen_controller_octave(&s,1,0,&ctx,&e)==GC_OCTAVE_OWNED);
 }
 C(gen_controller_octave(&s,0,1,&ctx,&e)==GC_OCTAVE_OWNED);C(e.value==2);
 C(gen_controller_button(&s,GG_SHIFT,0,&ctx,&e));s.gesture.active=0;
 C(gen_controller_octave(&s,0,0,&ctx,&e)==GC_OCTAVE_OWNED);C(!s.octave_held&&!s.octave_owned);
 C(init(&s,defaults));C(gen_controller_button(&s,GG_CHORD,1,&ctx,&e));
 C(gen_controller_note(&s,60,1,&ctx,&e));C(gen_controller_button(&s,GG_SHIFT,1,&ctx,&e));
 C(gen_controller_octave(&s,1,1,&ctx,&e)==GC_OCTAVE_OWNED);
 C((e.flags&GC_REJECTED)&&e.reject_reason==GG_CAPTURE_ACTIVE&&s.capture_count==1&&!s.core.pending_count);
 C(gen_controller_octave(&s,1,0,&ctx,&e)==GC_OCTAVE_OWNED);
 C(gen_controller_button(&s,GG_SHIFT,0,&ctx,&e));C(gen_controller_button(&s,GG_CHORD,0,&ctx,&e));C(e.flags&GC_CAPTURE_COMPLETE);
 C(init(&s,defaults));C(gen_controller_button(&s,GG_SHIFT,1,&ctx,&e));
 s.core.pending_count=UINT32_MAX;s.core.next_current=s.core.current;
 C(gen_controller_octave(&s,1,1,&ctx,&e)==GC_OCTAVE_OWNED);C((e.flags&GC_REJECTED)&&e.value==0&&s.core.next_current.params.range==0);
 C(gen_controller_octave(&s,1,0,&ctx,&e)==GC_OCTAVE_OWNED);
 C(!gen_controller_octave(&s,2,1,&ctx,&e));return 1;
}
static int leds(void){
 GenKeyledState s={0};GenKeyledInput i={0};
 for(unsigned n=0;n<41;n++)C(gk_stock_pixel(&s,&i,n,100+n)==100+n);
 C(gk_baseline_ready(&s));i.active=1;
 for(unsigned L=1;L<=64;L++)for(int step=-1;step<(int)L;step++){
  gk_project_steps(&i,UINT64_MAX,L,step);
  unsigned page=step<0?0:(unsigned)step/16,start=page*16,valid=L-start<16?L-start:16;
  C(i.page==page&&i.valid_steps==valid&&i.step==(step<0?-1:step%16));
  for(unsigned n=0;n<16;n++){
   uint32_t c=gk_stock_pixel(&s,&i,n+2,100+n+2);
   C(c==(n>=valid?0:i.step==(int)n?GK_HIT_CURSOR:GK_HIT));
  }
 }
 gk_project_steps(&i,UINT64_MAX,17,17);C(i.page==0&&i.step==-1&&i.valid_steps==16);
 gk_project_steps(&i,UINT64_MAX,65,0);C(!i.valid_steps&&i.step==-1&&!i.hits);
 gk_project_steps(&i,1ULL<<63,64,63);C(i.page==3&&i.hits==0x8000&&i.step==15);
 gk_project_steps(&i,0,1,0);C(gk_stock_pixel(&s,&i,2,102)==GK_CURSOR);
 i.capturing=1;i.range_valid=1;i.lower=60;i.count=2;const uint8_t pitches[]={60,96};i.notes=pitches;
 C(gk_stock_pixel(&s,&i,2,102)==GK_CAPTURE&&gk_stock_pixel(&s,&i,38,138)==GK_CAPTURE);
 i.fault=1;C(!gk_stock_pixel(&s,&i,2,102));i.active=0;C(gk_stock_pixel(&s,&i,2,102)==102);
 C(gk_stock_pixel(&s,&i,40,140)==140);
 return 1;
}
int main(void){if(!mappings()||!pages()||!commands()||!modes()||!panel()||!octave()||!leds())return 1;
 printf("PASS %u V4 controls/panel/keyLED assertions\n",checks);return 0;}

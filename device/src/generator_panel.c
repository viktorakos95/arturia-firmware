#include "generator_panel.h"
#include "generator_rate12.h"
static void text3(uint8_t *out,const char *s){for(unsigned i=0;i<3;++i)out[i]=(uint8_t)s[i];}
static void number(uint8_t *out,unsigned n){if(n>999)n=999;out[0]=(uint8_t)('0'+n/100);out[1]=(uint8_t)('0'+n/10%10);out[2]=(uint8_t)('0'+n%10);}
static unsigned alive(uint32_t now,uint32_t then,unsigned duration){return (uint32_t)(now-then)<duration;}
static void clear(GenPanelState *s){s->notice_valid=s->parameter_valid=s->pulse_valid=0;}
static void notice(GenPanelState *s,uint32_t tick,const char *text,unsigned pulse){
 /* A command replaces the previous knob hint. A later knob event clears this
  * notice in turn, so a command result cannot expire behind an older knob. */
 s->parameter_valid=0;
 text3(s->notice,text);s->notice_tick=tick;s->notice_valid=1;s->pulse_valid=(uint8_t)pulse;s->pulse_tick=tick;
}
void gp_rate_notice12(GenPanelState *s,uint32_t now){
 /* Caller has an active GEN owner. A RATE movement may precede the first
  * display snapshot after entry; it is newer than the generic entry notice. */
 clear(s);s->active=1;s->diagnostic_code=0;
 s->parameter=GCP_RATE;s->parameter_tick=now;s->parameter_valid=1;
}
void gp_diagnostic(GenPanelState *s,unsigned code,uint32_t now){
 if(code>99)return;
 s->diagnostic_code=(uint16_t)code;s->diagnostic_tick=now;
}
unsigned gp_capturing(const GenController *c){
 const GenGestureOwner *o=&c->gesture.owners[GG_CHORD];
 return c->gesture.active&&(c->gesture.held&(1u<<GG_CHORD))&&
   (c->capture_count||(o->kind==GG_OWNER_PREFIX&&!(o->flags&(GG_CANCELLED|GG_ATTEMPTED))));
}
void gp_update(GenPanelState *s,const GenPanelInput *i){
 unsigned changed=s->active!=i->active;
 if(changed){s->diagnostic_code=0;s->page_valid=0;}
 if(changed||(i->flags&GC_QUEUED&&i->action==GG_RESET))clear(s);
 s->active=i->active;
 if(i->capturing&&!s->capturing){clear(s);s->capture_tick=i->now;}
 s->capturing=i->capturing;
 if(changed&&i->active)notice(s,i->now,"GEn",0);
 if((i->flags&GC_PARAMETER)&&i->active&&(i->parameter<GC_CONTROLS||i->parameter==GCP_MODE||i->parameter==GCP_RANGE)){
  clear(s);s->parameter=i->parameter;s->parameter_tick=i->event_tick;s->parameter_valid=1;
 }
 if(i->flags&GC_REJECTED)notice(s,i->event_tick,"---",0);
 /* GC_QUEUED also accompanies parameter changes and completed captures;
  * their action field may still hold an older button command. */
 if((i->flags&GC_QUEUED)&&i->pending&&!(i->flags&(GC_PARAMETER|GC_CAPTURE_COMPLETE))){
  if(i->action==GG_VARY)notice(s,i->event_tick,"GEn",0);
  if(i->action==GG_COMMIT)notice(s,i->event_tick,"SEd",0);
  if(i->action==GG_RESET)notice(s,i->event_tick,"rSt",0);
 }
 if((i->flags&GC_CAPTURE_COMPLETE)&&i->pending){
  s->parameter_valid=0;notice(s,i->event_tick,"000",0);number(s->notice,i->complete_count);
 }
 if((i->flags&GC_APPLIED)&&i->result.valid){
  const GenResult *r=&i->result;
  if(r->action==GEN_COMMIT||r->action==GEN_NEW_SEED)notice(s,i->event_tick,"SEd",1);
  else if(r->action==GEN_RESET)notice(s,i->event_tick,"rSt",1);
  else if(r->action==GEN_VARY){
   const char *why=r->reason==GEN_EVOLVE_ZERO?"E00":r->reason==GEN_EMPTY?"d00":r->reason==GEN_SINGLE?"n01":"000";
   notice(s,i->event_tick,why,r->changed&&r->changed_steps);
   if(r->changed&&r->changed_steps)number(s->notice,r->changed_steps);
  }
 }
 /* Retain the observed LED page without creating display notices. */
 if(i->active&&i->page_valid&&i->page<4){
  s->page=i->page;s->page_valid=1;
 }else s->page_valid=0;
}
static uint8_t glyph(unsigned c){
 static const uint8_t digits[10]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f};
 if(c>='0'&&c<='9')return digits[c-'0'];
 switch(c){case 'G':return 0x3d;case 'E':return 0x79;case 'n':return 0x54;
 case 'S':return 0x6d;case 'd':return 0x5e;case 'r':return 0x50;
 case 'L':return 0x38;case 'A':return 0x77;case 'O':return 0x3f;
 case 'b':return 0x7c;case 'C':return 0x39;case 'Y':return 0x6e;case 'u':return 0x1c;
 case 't':return 0x78;case 'P':return 0x73;case 'F':return 0x71;case '-':return 0x40;default:return 0;}
}
void gp_view_rate12(const GenPanelState *s,const GenPanelInput *i,GenPanelView *v,unsigned bpm){
 static const char modes[8][4]={"rPt","CYC","ArC","ACC","AnS","brn","SYn","rnd"};
 v->display_owned=i->active;v->color_owned=i->active;v->pending=i->pending;v->pattern=GP_STEADY;v->brightness=i->active?191:255;
 text3(v->text,"GEn");
 if(i->active&&i->mode<8)text3(v->text,modes[i->mode]);
 if(!i->active&&s->diagnostic_code&&alive(i->now,s->diagnostic_tick,5000)){
  v->display_owned=1;v->text[0]='E';v->text[1]=(uint8_t)('0'+s->diagnostic_code/10);
  v->text[2]=(uint8_t)('0'+s->diagnostic_code%10);
 }else if(i->capturing)number(v->text,i->capture_count);
 else if(i->active&&s->parameter_valid&&alive(i->now,s->parameter_tick,2000)){
  /* Keep name and value visible together. A label-first phase restarted on
   * every movement and hid the value for the entire continuous gesture. */
  static const uint8_t labels[GC_CONTROLS]={'L','d','r','E','n','A','b','t'};
  if(s->parameter<GC_CONTROLS){number(v->text,i->values[s->parameter]);v->text[0]=labels[s->parameter];}
  else if(s->parameter==GCP_RATE&&bpm>=30&&bpm<=240)number(v->text,bpm);
  else if(s->parameter==GCP_MODE&&i->mode<8)text3(v->text,modes[i->mode]);
  else if(s->parameter==GCP_RANGE){number(v->text,i->range);v->text[0]='O';}
 }else if(s->notice_valid&&alive(i->now,s->notice_tick,s->notice_valid==2?400:1600)){
  for(unsigned n=0;n<3;++n)v->text[n]=s->notice[n];v->display_owned=1;
 }
 if(i->active&&i->capturing){
  /* 33-point cosine samples for one 1600ms cycle, integer interpolation. */
  static const uint8_t wave[33]={140,141,144,150,157,166,176,186,198,209,220,230,239,246,251,254,255,254,251,246,239,230,220,209,198,186,176,166,157,150,144,141,140};
  unsigned phase=(uint32_t)(i->now-s->capture_tick)%1600,n=phase/50,part=phase%50;
  v->brightness=(uint8_t)(((unsigned)wave[n]*(50-part)+(unsigned)wave[n+1]*part+25)/50);v->pattern=GP_CAPTURE;
 }else if(i->active&&s->pulse_valid&&alive(i->now,s->pulse_tick,160)){v->brightness=255;v->pattern=GP_CONFIRM;}
 for(unsigned n=0;n<3;++n)v->glyphs[n]=glyph(v->text[n]);
 if(v->display_owned&&i->active&&i->pending)v->glyphs[2]|=0x80;
 unsigned intensity=(42u*v->brightness+127)/255;
 v->color=(intensity<<16)|(intensity<<8); /* packed 00/green/blue: provisional cyan */
 if(i->fault){
  v->display_owned=v->color_owned=1;v->pending=0;v->pattern=GP_STEADY;v->brightness=255;v->color=42;
  unsigned code=i->fault<=99?i->fault:99;
  v->text[0]='F';v->text[1]=(uint8_t)('0'+code/10);v->text[2]=(uint8_t)('0'+code%10);
  for(unsigned n=0;n<3;++n)v->glyphs[n]=glyph(v->text[n]);
 }
}
/* Compatibility entry for callers without an authenticated tempo source. */
void gp_view(const GenPanelState *s,const GenPanelInput *i,GenPanelView *v){
 gp_view_rate12(s,i,v,0);
}
uint32_t gp_stock_pixel(GenPanelState *s,const GenPanelView *v,uint32_t original){
 s->stock_color=original;s->stock_valid=1;uint32_t color=v->color_owned?v->color:original;
 s->last_color=color;s->last_valid=1;return color;
}
unsigned gp_output_due(GenPanelState *s,const GenPanelView *v,uint32_t *color){
 if(!s->stock_valid)return 0;
 uint32_t desired=v->color_owned?v->color:s->stock_color;
 if(s->last_valid&&s->last_color==desired)return 0;
 s->last_color=desired;s->last_valid=1;*color=desired;return 1;
}

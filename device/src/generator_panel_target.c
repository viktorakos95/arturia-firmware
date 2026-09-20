#include "generator_device_runtime.h"
#include "fault_trace11.h"
#include "generator_rate12.h"
#if !defined(__thumb__)
#error "Panel target requires ARM Thumb"
#endif
extern GeneratorDeviceRuntime *ks37_generator_device_state(void);
extern void ks37_panel_writer_original(uint32_t,uint32_t,uint32_t);
/* Immutable byte sources keep the native SPI handle's retained TX pointer
 * valid after its synchronous call returns. This is NOT an RGB DMA buffer. */
static const uint8_t display_bytes[256]={
 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
 0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
 0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
 0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
 0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
 0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
 0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
 0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
 0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
 0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
 0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
 0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
 0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
 0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff,
};
/* All reads/consumption are inside a preserved PRIMASK critical section. The
 * native SPI send itself remains outside it and keeps the original mask. */
static void snapshot(GeneratorDeviceRuntime *s,GenPanelInput *i,GenPanelView *v){
 const GenController *c=&s->base.controller;
 i->now=gd_hw_tick();i->event_tick=s->feedback_dirty?s->feedback_tick:i->now;
 i->flags=(uint16_t)s->base.pending_feedback;i->active=c->gesture.active;
 i->capturing=(uint8_t)gp_capturing(c);i->capture_count=c->capture_count;
 i->complete_count=s->panel.capture_complete_count;
 i->action=(uint8_t)s->feedback_action;i->parameter=(uint8_t)s->last_parameter;
 for(unsigned n=0;n<GC_CONTROLS;++n)i->values[n]=(uint8_t)gen_controller_parameter_value(c,n);
 i->mode=gen_controller_parameters(c).mode;
 i->range=gen_controller_parameters(c).range;
 i->step=-1;i->page=0;i->page_valid=0;
 if(s->device.bridge.have_step&&s->device.bridge.last_step<c->core.current.params.length){
  i->step=(int8_t)s->device.bridge.last_step;
  i->page=(uint8_t)((unsigned)i->step/16u);i->page_valid=1;
 }
 i->pending=(uint8_t)(c->core.pending_count!=0);i->result=gen_feedback(&c->core.last_feedback);
 i->fault=(uint8_t)((s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)?gd_trace_code11(s):0);
 gp_update(&s->panel,i);
 unsigned bpm=0;
 if(i->active&&!i->fault&&s->panel.parameter_valid&&s->panel.parameter==GCP_RATE&&
    (uint32_t)(i->now-s->panel.parameter_tick)<2000u&&
    *(const volatile uint32_t *)(uintptr_t)0x20001098u==0x20002becu&&
    !*(const volatile uint8_t *)(uintptr_t)0x20002bf8u){
  /* Native getter12280 reads C+e. Setter12048 clamps3000..24000;
   * normal display1c7a8 divides by100, yielding integer BPM30..240.
   * Read after native processing on every visible frame; raw control
   * interception runs before that setter and cannot provide its new value. */
  unsigned hundredths=*(const volatile uint16_t *)(uintptr_t)0x20002bfau;
  if(hundredths>=3000&&hundredths<=24000)bpm=hundredths/100u;
 }
 gp_view_rate12(&s->panel,i,v,bpm);
 s->base.pending_feedback=0;s->feedback_dirty=0;
}
/* Read only presentation state. This does not consume panel feedback, mutate
 * the selected note or borrow the native128-note flags as an LED bitmap. */
static void keyled_snapshot(const GeneratorDeviceRuntime *s,GenKeyledInput *i){
 const GenController *c=&s->base.controller;
 const volatile uint8_t *k=(const volatile uint8_t *)(uintptr_t)0x200013fcu;
 unsigned shift=*(const volatile uint8_t *)(uintptr_t)0x20005285u?12u:0u;
 int raw_low=(int8_t)k[0x950],raw_high=(int8_t)k[0x951];
 int low=(int8_t)(uint8_t)(k[0x950]-shift);
 int high=(int8_t)(uint8_t)(k[0x951]-shift);
 i->notes=c->capture;i->count=c->capture_count;
 i->lower=(int16_t)low;i->range_valid=(uint8_t)(raw_high-raw_low==37&&high-low==37);
 int global_step=s->device.bridge.have_step?(int)s->device.bridge.last_step:-1;
 gk_project_steps(i,c->core.current.active,c->core.current.params.length,global_step);
 /* Retain ownership after a partial publication fault, like native guards. */
 i->active=(uint8_t)(c->gesture.active||s->device.phase!=PD_OFF);
 i->capturing=(uint8_t)gp_capturing(c);
 i->fault=(uint8_t)!!(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT);
}
const uint8_t *ks37_panel_display_source(uint32_t receiver,uint32_t index,const uint8_t *original){
 uint32_t mask=pd_hw_irq_save();const uint8_t *out=original;
 GeneratorDeviceRuntime *s=ks37_generator_device_state();
 if(s&&receiver==0x200023d0u&&index<3){
  GenPanelInput i;GenPanelView v;snapshot(s,&i,&v);
  if(v.display_owned)out=&display_bytes[v.glyphs[index]];
 }
 pd_hw_irq_restore(mask);return out;
}
uint32_t ks37_panel_pixel(uint32_t receiver,uint32_t index,uint32_t original){
 uint32_t mask=pd_hw_irq_save(),out=original;
 GeneratorDeviceRuntime *s=ks37_generator_device_state();
 if(s&&receiver==0x200013fcu&&index<GK_PIXELS){
  GenKeyledInput keys;keyled_snapshot(s,&keys);
  out=gk_stock_pixel(&s->keyled,&keys,index,original);
  if(index==0){
   GenPanelInput i;GenPanelView v;snapshot(s,&i,&v);
   out=gp_stock_pixel(&s->panel,&v,original);
  }
 }
 pd_hw_irq_restore(mask);return out;
}
void gd_hw_feedback(GeneratorDeviceRuntime *s){
 if(!s)return;
 uint32_t mask=pd_hw_irq_save(),color=0;
 GenPanelInput i;GenPanelView v;snapshot(s,&i,&v);
 if(gp_output_due(&s->panel,&v,&color))
  ks37_panel_writer_original(0x200013fcu,0,color);
 pd_hw_irq_restore(mask);
 /* Sweep only from ordinary IRQ-enabled main context. Each changed pixel has
  * its own bounded critical section, with IRQs admitted between pixels. Stock
  * callbacks can safely interleave because their latest intent is shadowed.
  * Existing live DMA can still observe intermediate frames. */
 if(mask||pd_hw_ipsr())return;
 for(unsigned index=1;index<=39;++index){
  mask=pd_hw_irq_save();GenKeyledInput keys;keyled_snapshot(s,&keys);
  if(gk_output_due(&s->keyled,&keys,index,&color))
   ks37_panel_writer_original(0x200013fcu,index,color);
  pd_hw_irq_restore(mask);
 }
}

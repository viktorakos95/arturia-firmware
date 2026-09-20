#include "generator_device_runtime.h"
#include "fault_trace11.h"
#if !defined(__thumb__)
#error "Time Div witnesses require ARM Thumb"
#endif
extern GeneratorDeviceRuntime *ks37_generator_device_state(void);
/* Both assembly witnesses own their entire short native store sequence under
 * a saved mask. No transport, NoteOff or publication is invoked here. */
void ks37_time_division_request(uint32_t receiver,unsigned value) {
    GeneratorDeviceRuntime *s=ks37_generator_device_state();
    if(s)if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
    PdResult r=s?pd_time_division_request_locked(&s->device,&s->prepared,receiver,value):PD_PASS;
    if(r==PD_OK||r==PD_PASS) {
        *(volatile uint8_t *)(uintptr_t)(receiver+0x49)=(uint8_t)value;
        *(volatile uint8_t *)(uintptr_t)(receiver+0x54)=1;
    } else if(s){gd_trace_fault11(s,19,r);s->base.faults|=GD_PUBLICATION_FAULT;}
}
void ks37_time_division_applied(uint32_t receiver,unsigned value,uint32_t phase,uint32_t reload) {
    GeneratorDeviceRuntime *s=ks37_generator_device_state();
    if(!s)return;
    if(s->base.faults||s->buttons.faults||s->device.phase==PD_FAULT)(void)gd_trace_code11(s);
    PdResult r=pd_time_division_applied_locked(&s->device,&s->prepared,receiver,value,phase,reload);
    if(r!=PD_OK&&r!=PD_PASS){gd_trace_fault11(s,39,r);s->base.faults|=GD_PUBLICATION_FAULT;}
}

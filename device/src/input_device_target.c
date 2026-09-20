#include "input_device.h"
#if !defined(__thumb__)
#error "Target input adapter requires ARM Thumb"
#endif
static uint32_t save(void) {
    uint32_t v;__asm__ volatile("mrs %0,primask\ncpsid i":"=r"(v)::"memory");return v;
}
static void restore(uint32_t v){__asm__ volatile("msr primask,%0"::"r"(v):"memory");}
static uint32_t word(uint32_t p){return *(const volatile uint32_t *)(uintptr_t)p;}
static uint8_t byte(uint32_t p){return *(const volatile uint8_t *)(uintptr_t)p;}
static void zero(void *p,unsigned n){volatile uint8_t *b=p;while(n--)*b++=0;}
static int binding(InputDeviceBinding *b) {
    zero(b,sizeof(*b));
    return ks37_input_device_bind(b) && b->ready && b->state && b->history && b->deliver;
}
static void deliver(InputDeviceBinding *b,int changed,const IhEffects *e) {
    if(changed)b->deliver(b->context,e);
}
static unsigned pristine_usb(void) {
    const uint32_t m=0x200023f8;
    if(word(0x200010e0)!=m || word(m)!=0x0801e9a8 ||
       word(m+0x140) || word(m+0x144) || word(m+0x5c)!=m ||
       word(m+0x64)!=1 || word(m+0x68))return 0;
    for(unsigned i=0;i<6;++i)if(byte(m+0x138+i))return 0;
    return 1;
}
uint32_t ks37_input_cpuid_read(void) {return word(0xe000ed00);}
void ks37_input_cold_entry(uint32_t object) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)) {
        InputDeviceCold c;zero(&c,sizeof(c));IhEffects effects[3];
        const uint32_t m=0x200023f8,s=0x20000674;
        c.object=object;c.gate=word(0x200010e0);c.vtable=word(m);
        c.usb_self=word(m+0x5c);c.din_self=word(m+0x24c);
        c.usb_count=word(m+0x64);c.din_count=word(m+0x254);
        c.usb_tag=word(m+0x68);c.din_tag=word(m+0x258);
        c.usb_write=word(m+0x140);c.usb_read=word(m+0x144);
        c.din_write=word(m+0x330);c.din_read=word(m+0x334);
        c.scanner_source=word(s+4);
        for(unsigned i=0;i<6;++i) {
            c.parser[i]=byte(m+0x138+i);c.parser[i+6]=byte(m+0x328+i);
            c.scanner_notes[i]=word(s+0x108+4*i);
        }
        for(unsigned i=0;i<37;++i)c.scanner_state[i]=byte(s+0x4d+i);
        if(input_device_cold(b.state,b.history,&c,effects))
            for(unsigned i=0;i<3;++i)b.deliver(b.context,&effects[i]);
        else if(b.state->active)
            deliver(&b,input_device_loss(b.state,b.history,7,ID_F_COLD,&effects[0]),&effects[0]);
    }
    restore(mask);
    /* The assembly wrapper still has its OUTER saved mask held. It performs
     * the original M publication exactly once before restoring that mask. */
}
uint32_t ks37_input_usb_ingress_entry(uint32_t object,const uint8_t *packet,uint32_t length) {
    uint32_t mask=save();InputDeviceBinding b;unsigned have=binding(&b);
    uint32_t before=0,read_before=0;unsigned expected=0,shape=0;
    if(have && object==0x200023f8) {
        before=word(object+0x140);read_before=word(object+0x144);
        shape=input_device_usb_expected(packet,length,&expected);
    }
    typedef uint32_t (*Native)(uint32_t,const uint8_t *,uint32_t);
    uint32_t result=((Native)(uintptr_t)0x08010639)(object,packet,length);
    if(have) {
        IhEffects e;
        if(object==0x200023f8)
            deliver(&b,input_device_usb_result(b.state,b.history,before,word(object+0x140),
                read_before,word(object+0x144),expected,shape,result,&e),&e);
        else deliver(&b,input_device_loss(b.state,b.history,1,ID_F_PROFILE,&e),&e);
    }
    restore(mask);return result;
}
void ks37_input_uart_status_entry(uint32_t receiver,uint32_t status) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)){IhEffects e;deliver(&b,input_device_uart(b.state,b.history,receiver,status,&e),&e);}
    restore(mask);
}
void ks37_input_usb_flags_entry(uint32_t receiver,uint32_t flags) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)){IhEffects e;deliver(&b,input_device_usb_flags(b.state,b.history,receiver,flags,pristine_usb(),&e),&e);}
    restore(mask);
}
void ks37_input_usb_deinit_entry(uint32_t receiver) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)){IhEffects e;deliver(&b,input_device_usb_deinit(b.state,b.history,receiver,pristine_usb(),&e),&e);}
    restore(mask);
}
void ks37_input_acquire_begin_entry(uint32_t receiver) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)) {
        input_device_frame_begin(b.state);
        if(receiver!=0x20000410)b.state->faults|=ID_F_FRAME;
    }
    restore(mask);
}
void ks37_input_acquire_end_entry(uint32_t receiver) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)) {
        InputDeviceFrame f;zero(&f,sizeof(f));f.object=receiver;
        f.heap_begin=b.heap_begin;f.heap_end=b.heap_end;
        if(receiver==0x20000410) {
            f.vtable=word(receiver);f.buffer=word(receiver+4);f.length=byte(receiver+8);
            f.output_port=word(receiver+0xc);f.output_shift=byte(receiver+0x10);
            f.input_port=word(receiver+0x14);f.input_shift=byte(receiver+0x18);
            f.scanner_source=word(0x20000678);
            if(f.buffer>=f.heap_begin && f.heap_end>=f.heap_begin &&
               f.buffer<=f.heap_end && f.heap_end-f.buffer>=10)
                for(unsigned i=0;i<10;++i)f.raw[i]=byte(f.buffer+i);
        }
        if(!input_device_frame_complete(b.state,&f)) {
            IhEffects e;deliver(&b,input_device_loss(b.state,b.history,4,ID_F_FRAME,&e),&e);
        }
    }
    restore(mask);
}
void ks37_input_scan_end_entry(uint32_t receiver) {
    uint32_t mask=save();InputDeviceBinding b;
    if(binding(&b)) {
        input_device_scan_complete(b.state,receiver);
        if(!b.state->frame_valid){IhEffects e;deliver(&b,input_device_loss(b.state,b.history,4,ID_F_FRAME,&e),&e);}
    }
    restore(mask);
}

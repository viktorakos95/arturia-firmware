#include "input_device.h"

static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
void input_device_init(InputDeviceState *s,unsigned profile) {
    clear(s,sizeof(*s));s->profile=profile;
    if(profile!=ID_PROFILE_STOCK_RM0008)s->faults|=ID_F_PROFILE;
}
static int pristine(const IhState *h) {
    if(h->known_sources || h->seen_sources || h->held_count)return 0;
    for(unsigned i=0;i<3;++i)if(h->epochs[i] || h->faults[i])return 0;
    for(unsigned i=0;i<IH_CAPACITY;++i)if(h->owners[i].count)return 0;
    for(unsigned i=0;i<128;++i)if(h->pitch_count[i])return 0;
    for(unsigned i=0;i<37;++i)if(h->scanner_key_owner[i])return 0;
    return 1;
}
int input_device_cold(InputDeviceState *s,IhState *h,const InputDeviceCold *c,IhEffects e[3]) {
    if(!s || !h || !c || !e)return 0;
    unsigned already=s->cold_used;s->cold_used=1;
    int ok=!already && !s->faults && !s->active && pristine(h) && !c->gate &&
      c->object==0x200023f8 && c->vtable==0x0801e9a8 &&
      c->usb_self==c->object && c->din_self==c->object &&
      c->usb_count==1 && c->din_count==1 && c->usb_tag==0 && c->din_tag==1 &&
      !c->usb_write && !c->usb_read && !c->din_write && !c->din_read &&
      c->scanner_source==0x20000410;
    for(unsigned i=0;i<12;++i)if(c->parser[i])ok=0;
    for(unsigned i=0;i<37;++i)if(c->scanner_state[i])ok=0;
    for(unsigned i=0;i<6;++i)if(c->scanner_notes[i])ok=0;
    if(!ok){s->faults|=ID_F_COLD;return 0;}
    for(unsigned source=0;source<3;++source) {
        IhBaselineProof p={1,IH_PROOF_ALL};
        if(ih_accept_empty_source(h,source,p,&e[source])!=IH_APPLIED) {
            s->faults|=ID_F_COLD;return 0;
        }
    }
    s->active=1;return 1;
}
int input_device_loss(InputDeviceState *s,IhState *h,unsigned sources,unsigned reason,IhEffects *e) {
    if(!s || !h || !e || !sources || (sources&~7u))return 0;
    /* Before the cold receiver gate opens, no old accepted holds exist. */
    if(!s->active)return 0;
    s->faults|=reason;s->lost_sources|=(uint8_t)sources;
    if(s->losses!=UINT32_MAX)++s->losses;
    return ih_lose_continuity(h,sources,IH_STREAM_LOST,e)==IH_FAULT;
}
int input_device_uart(InputDeviceState *s,IhState *h,uint32_t receiver,uint32_t status,IhEffects *e) {
    if(!s)return 0;s->uart_status_seen|=status;
    if(receiver!=0x40013800 || s->profile!=ID_PROFILE_STOCK_RM0008)
        return input_device_loss(s,h,2,ID_F_PROFILE,e);
    return (status&15)?input_device_loss(s,h,2,ID_F_UART,e):0;
}
static int initial_usb(const InputDeviceState *s,const IhState *h,unsigned raw_pristine) {
    if(!raw_pristine || !s->active || s->usb_started || (s->lost_sources&1) ||
       !h || (h->seen_sources&1) || h->faults[0] || h->epochs[0]!=1 ||
       !(h->known_sources&1))return 0;
    for(unsigned i=0;i<IH_CAPACITY;++i)
        if(h->owners[i].count && (h->owners[i].key>>11)==IH_USB)return 0;
    return 1;
}
int input_device_usb_flags(InputDeviceState *s,IhState *h,uint32_t receiver,uint32_t flags,unsigned raw_pristine,IhEffects *e) {
    if(!s)return 0;s->usb_flags_seen|=flags;
    if(receiver!=0x40005c00 || s->profile!=ID_PROFILE_STOCK_RM0008)
        return input_device_loss(s,h,1,ID_F_PROFILE,e);
    /* PMA over/underrun, error, reset, suspend. SOF/ESOF/CTR are not loss. */
    if((flags&0x6c00) && !initial_usb(s,h,raw_pristine))
        return input_device_loss(s,h,1,ID_F_USB_LIFE,e);
    return 0;
}
int input_device_usb_deinit(InputDeviceState *s,IhState *h,uint32_t receiver,unsigned raw_pristine,IhEffects *e) {
    if(s && receiver==0x20005624 && initial_usb(s,h,raw_pristine))return 0;
    return input_device_loss(s,h,1,receiver==0x20005624?ID_F_USB_LIFE:ID_F_PROFILE,e);
}
int input_device_usb_expected(const uint8_t *p,unsigned length,unsigned *count) {
    static const uint8_t sizes[16]={0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};
    if(!count || (!p && length) || length>64 || (length&3))return 0;
    unsigned n=0;for(unsigned i=0;i<length;i+=4)n+=sizes[p[i]&15];
    *count=n;return 1;
}
int input_device_usb_result(InputDeviceState *s,IhState *h,uint32_t before,uint32_t after,
    uint32_t read_before,uint32_t read_after,unsigned expected,unsigned shape_ok,unsigned result,IhEffects *e) {
    if(!s)return 0;if(s->usb_calls!=UINT32_MAX)++s->usb_calls;
    if(expected || !shape_ok)s->usb_started=1;
    if(!shape_ok || before>255 || after>255 || read_before>255 || read_after>255 ||
       read_before!=read_after || expected>48 || result!=1 || ((after-before)&255)!=expected)
        return input_device_loss(s,h,1,ID_F_USB_DROP,e);
    return 0;
}
void input_device_frame_begin(InputDeviceState *s) {
    if(!s)return;s->frame_valid=0;
    if(s->acquiring)s->faults|=ID_F_REENTRY;
    s->acquiring=1;
}
int input_device_frame_complete(InputDeviceState *s,const InputDeviceFrame *f) {
    if(!s || !f)return 0;
    unsigned entered=s->acquiring;s->acquiring=0;s->frame_valid=0;
    if(!entered || (s->faults&(ID_F_FRAME|ID_F_REENTRY|ID_F_EPOCH)) ||
       f->object!=0x20000410 || f->vtable!=0x0801e0ac || f->length!=10 ||
       f->output_port!=0x40011800 || f->output_shift!=8 ||
       f->input_port!=0x40011400 || f->input_shift!=8 || f->scanner_source!=f->object ||
       f->buffer<f->heap_begin || f->heap_end<f->heap_begin ||
       f->buffer>f->heap_end || f->heap_end-f->buffer<10) {
        s->faults|=ID_F_FRAME;return 0;
    }
    if(s->frame_epoch==UINT32_MAX){s->faults|=ID_F_EPOCH;return 0;}
    uint32_t lo=0;uint8_t hi=0;
    for(unsigned k=0;k<37;++k) {
        unsigned row=(k+3)>>3,bit=(k+3)&7;
        if(((f->raw[2*row]|f->raw[2*row+1])>>bit)&1) {
            if(k<32)lo|=1u<<k;else hi|=(uint8_t)(1u<<(k-32));
        }
    }
    s->down_lo=lo;s->down_hi=hi;++s->frame_epoch;s->frame_valid=1;return 1;
}
void input_device_scan_complete(InputDeviceState *s,uint32_t scanner) {
    if(!s)return;
    if(scanner!=0x20000674 || !s->frame_valid || s->acquiring || !s->frame_epoch ||
       s->consumed_epoch==s->frame_epoch) {s->faults|=ID_F_FRAME;s->frame_valid=0;return;}
    s->consumed_epoch=s->frame_epoch;
}
int input_device_physical_known(const InputDeviceState *s,uint32_t epoch) {
    return s && s->active && s->frame_valid && !s->acquiring && epoch &&
        s->frame_epoch==epoch && !(s->faults&(ID_F_FRAME|ID_F_REENTRY|ID_F_EPOCH|ID_F_COLD|ID_F_PROFILE));
}
int input_device_physical_empty(const InputDeviceState *s,uint32_t epoch) {
    return input_device_physical_known(s,epoch) && !s->down_lo && !s->down_hi;
}

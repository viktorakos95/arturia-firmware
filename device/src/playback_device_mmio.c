#include "playback_device.h"
#if !defined(__arm__) || !defined(__thumb__)
#error "ARM-only device binding; host tests must provide substitutes"
#endif
uint32_t pd_hw_ipsr(void) { uint32_t v; __asm__ volatile("mrs %0,ipsr":"=r"(v)); return v; }
uint32_t pd_hw_control(void) { uint32_t v; __asm__ volatile("mrs %0,control":"=r"(v)); return v; }
uint32_t pd_hw_irq_save(void) {
    uint32_t v;
    __asm__ volatile("mrs %0,primask\n\tcpsid i":"=r"(v)::"memory");
    return v;
}
void pd_hw_irq_restore(uint32_t v) { __asm__ volatile("msr primask,%0"::"r"(v):"memory"); }
uint8_t pd_hw_read8(uint32_t a) { return *(volatile const uint8_t *)(uintptr_t)a; }
uint32_t pd_hw_read32(uint32_t a) { return *(volatile const uint32_t *)(uintptr_t)a; }
uint8_t *pd_hw_block(uint32_t a) { return (uint8_t *)(uintptr_t)a; }
void pd_hw_store_current(uint32_t a) {
    /* Final data publication is one aligned word store; neither S.pending nor
     * N's saved pitches are touched. Not a source-lifetime proof by itself. */
    __asm__ volatile("dmb":::"memory");
    *(volatile uint32_t *)(uintptr_t)0x20002c4cu = a;
    __asm__ volatile("dmb":::"memory");
}
void pd_hw_cleanup(void) {
    ((void (*)(uint32_t))(uintptr_t)0x08014259u)(0x20004ed4u);
}
void pd_hw_service(void) {
    ((void (*)(uint32_t))(uintptr_t)0x080129cdu)(0x20002becu);
}
extern void ks37_playback_stock_transport(uint32_t, uint32_t);
void pd_hw_stop(void) { ks37_playback_stock_transport(0x20002becu, 0); }
uint32_t pd_hw_timer_word(unsigned offset) {
    uint32_t timer=pd_hw_read32(0x200054bcu);
    return pd_hw_read32(timer+offset);
}

PdResult pd_hw_publish_length(uint32_t next,unsigned length,uint32_t reload,
                              unsigned ticks,unsigned running) {
    /* Native18788/b020/b834 configure TIM4 as SMS7/TS1 (SMCR17), PSC0,
     * upcount, unbuffered ARR. Exact live profile is required, not inferred
     * from initialization alone. Trigger-mode6 could re-enable CEN itself;
     * it is deliberately excluded. This is a new bounded GEN transaction,
     * not an invocation or reimplementation of native Stop. */
    uint32_t timer=pd_hw_read32(0x200054bcu);
    if(timer!=0x40000800u || running>1 || length<1 || length>64 || !ticks ||
       reload!=ticks*length-1u || !next || (next&3u))return PD_STOCK_GUARD;
    volatile uint32_t *cr1=(volatile uint32_t *)(uintptr_t)timer;
    volatile uint32_t *cnt=(volatile uint32_t *)(uintptr_t)(timer+0x24);
    uint32_t control=*cr1;
    if(control!=running || pd_hw_read32(timer+8)!=0x17u ||
       (pd_hw_read32(timer+0x20)&0x1555u))return PD_STOCK_GUARD;
    /* PRIMASK does not freeze the peripheral. Stop only its counter while
     * preserving CNT/PSC/UG/status/DIER and the exact saved CR1. A clock pulse
     * during this short hold may be omitted; no whole transport Stop occurs. */
    *cr1=0;
    __asm__ volatile("dsb":::"memory");
    if(*cr1!=0) {
        *cr1=control;__asm__ volatile("dsb":::"memory");
        return PD_PUBLICATION_FAULT; /* Could not prove counter held. */
    }
    uint32_t phase=*cnt;
    if(phase>=ticks || (!running&&phase) || *cnt!=phase) {
        *cr1=control;__asm__ volatile("dsb":::"memory");
        return *cr1==control?PD_WAIT:PD_PUBLICATION_FAULT;
    }
    /* No hardware counter edge may now enter the read-to-ARR-store gap.
     * Keep the hold limited to these four publication stores, then resume
     * before full readback or controller feedback/copy. */
    __asm__ volatile("dmb":::"memory");
    *(volatile uint32_t *)(uintptr_t)0x20002c4cu=next;
    *(volatile uint8_t *)(uintptr_t)0x20002bfcu=(uint8_t)length;
    *(volatile uint32_t *)(uintptr_t)(timer+0x2c)=reload;
    *(volatile uint32_t *)(uintptr_t)0x200054c8u=reload;
    __asm__ volatile("dsb":::"memory");
    *cr1=control;
    __asm__ volatile("dsb":::"memory");
    return *cr1==control?PD_PUBLISHED:PD_PUBLICATION_FAULT;
}

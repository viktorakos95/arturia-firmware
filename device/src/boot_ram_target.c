#include "boot_ram.h"
#include <stdint.h>

/* These symbols must be owned by the image linker and covered by the patched
 * Reset zero range. Never include this target module without those patches. */
extern unsigned char __boot_payload_start[],__boot_payload_end[],__boot_ram_end[];
extern unsigned char __boot_stack_margin[];
__attribute__((section(".boot_ram.header"),used)) static BootRam state;
__attribute__((section(".boot_ram.guard_low"),used)) static uint32_t guard_low[BOOT_GUARD_WORDS];
__attribute__((section(".boot_ram.guard_high"),used)) static uint32_t guard_high[BOOT_GUARD_WORDS];

static volatile uint32_t *word(uint32_t address) { return (volatile uint32_t *)(uintptr_t)address; }
static uint32_t irq_save(void) {
    uint32_t mask; __asm__ volatile("mrs %0, primask\ncpsid i":"=r"(mask)::"memory"); return mask;
}
static void irq_restore(uint32_t mask) { __asm__ volatile("msr primask, %0"::"r"(mask):"memory"); }
static BootObservation observation(void) {
    BootObservation o;
    __asm__ volatile("mrs %0, msp":"=r"(o.msp));
    __asm__ volatile("mrs %0, control":"=r"(o.control));
    __asm__ volatile("mrs %0, ipsr":"=r"(o.ipsr));
    o.break_value=*word(0x200052a8u); o.heap_origin=*word(0x200052a4u);
    o.free_head=*word(0x200052a0u); return o;
}
static BootLayout layout(void) {
    BootLayout l={(uint32_t)(uintptr_t)__boot_payload_start,
        (uint32_t)((uintptr_t)__boot_payload_end-(uintptr_t)__boot_payload_start),
        (uint32_t)(uintptr_t)__boot_ram_end,(uint32_t)(uintptr_t)__boot_stack_margin};
    return l;
}
static BootRamView view(void) {
    BootRamView v={&state,guard_low,__boot_payload_start,guard_high}; return v;
}
void boot_ram_target_initialize(uint32_t entry_msp) {
    uint32_t mask=irq_save(); BootLayout l=layout(); BootObservation o=observation();
    /* Check both original hook SP and this C observation; the smaller sample
     * accounts for our current wrapper/frame without predicting future calls. */
    if(entry_msp<o.msp) o.msp=entry_msp;
    boot_ram_initialize(view(),&l,o,boot_payload_init);
    irq_restore(mask);
}
void *boot_payload_get(void) {
    uint32_t mask=irq_save(); BootLayout l=layout();
    void *result=boot_ram_payload(view(),&l,observation()); irq_restore(mask); return result;
}
const volatile BootRam *boot_ram_status_get(void) { return &state; }
uint32_t boot_ram_target_sbrk(int32_t delta) {
    uint32_t mask=irq_save(); BootLayout l=layout(); uint32_t old,next;
    int accepted=boot_ram_growth(&state,&l,observation(),delta,&old,&next);
    if(accepted) *word(0x200052a8u)=next;
    else { *word(0x20005ea8u)=12; old=UINT32_MAX; }
    irq_restore(mask); return old;
}

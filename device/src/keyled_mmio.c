#include "keyled_adapter.h"

#if !defined(__arm__) || !defined(__thumb__)
#error "keyled_mmio.c is the ARM device binding; host tests must provide fakes"
#endif

uint32_t kl_hw_ipsr(void) {
    uint32_t result;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(result));
    return result;
}
uint32_t kl_hw_irq_save(void) {
    uint32_t result;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r"(result) :: "memory");
    return result;
}
void kl_hw_irq_restore(uint32_t saved) {
    __asm__ volatile ("msr primask, %0" :: "r"(saved) : "memory");
}
void kl_hw_barrier(void) { __asm__ volatile ("dmb" ::: "memory"); }
uint32_t kl_hw_read32(uint32_t address) { return *(volatile const uint32_t *)(uintptr_t)address; }
uint8_t kl_hw_read8(uint32_t address) { return *(volatile const uint8_t *)(uintptr_t)address; }
void kl_hw_write32(uint32_t address, uint32_t value) { *(volatile uint32_t *)(uintptr_t)address = value; }
volatile const uint16_t *kl_hw_stock_wave(void) { return (volatile const uint16_t *)(uintptr_t)0x20001400u; }
uint32_t kl_hw_stock_start(uint32_t source) {
    typedef uint32_t (*StockStart)(uint32_t, uint32_t, uint32_t, uint32_t);
    return ((StockStart)(uintptr_t)0x0800b6edu)(0x200055a8u, 0, source, KL_SAMPLES);
}

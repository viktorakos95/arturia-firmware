#include "native_admission.h"

#if !defined(__arm__) && !defined(__thumb__)
#error "native_admission_read.c is an ARM-only hardware reader; do not link/run on host"
#endif

static const volatile uint8_t *ram(uint32_t address) {
    return (const volatile uint8_t *)(uintptr_t)address;
}

int ks37_native_read_device(Ks37NativeDeviceRead *out) {
    uint32_t ipsr, control, old_mask;
    out->read_evidence = 0;
    out->unresolved = KS37_NEED_INPUT_HISTORY | KS37_NEED_EXTERNAL_PENDING |
                      KS37_NEED_ATOMIC_TRANSITION;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr) :: "memory");
    __asm__ volatile("mrs %0, control" : "=r"(control) :: "memory");
    if (ipsr != 0 || (control & 1u)) return 0;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(old_mask) :: "memory");
    uint32_t u = *(const volatile uint32_t *)(uintptr_t)UINT32_C(0x20001124);
    uint32_t c = *(const volatile uint32_t *)(uintptr_t)UINT32_C(0x20001098);
    int valid = u == UINT32_C(0x20002ddc) && c == UINT32_C(0x20002bec);
    if (valid) {
        Ks37NativeSources sources = {
            ram(u), ram(c), ram(UINT32_C(0x200051cc)),
            ram(UINT32_C(0x200010b6)), ram(UINT32_C(0x200010c2)),
            ram(UINT32_C(0x200010e4)), ram(UINT32_C(0x200010d2))
        };
        ks37_native_snapshot_read(&out->snapshot, &sources);
        out->read_evidence = KS37_READ_MASKABLE_IRQS_HELD;
    }
    __asm__ volatile("msr primask, %0" :: "r"(old_mask) : "memory");
    return valid;
}

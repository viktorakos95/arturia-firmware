#include "generator_device_runtime.h"
#if !defined(__thumb__)
#error "Pitch target requires ARM Thumb"
#endif

extern GeneratorDeviceRuntime *ks37_generator_device_state(void);

unsigned ks37_generator_pitch_owned(void) {
    uint32_t mask=pd_hw_irq_save();
    GeneratorDeviceRuntime *s=ks37_generator_device_state();
    unsigned owned=(unsigned)gd_owns_native(s);
    pd_hw_irq_restore(mask);
    return owned;
}

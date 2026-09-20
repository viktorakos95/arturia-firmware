#include "control_input_policy.h"

uint32_t ci_stock_object(unsigned id) {
    /* The four proven bindings have stride0x78. Arithmetic avoids a relocated
     * data table in the minimal freestanding image plan. ID0 is separate. */
    if (!id) return 0x20000468u;
    return id <= 4 ? 0x2000121cu + (id - 1u) * 0x78u : 0;
}

CiResult ci_route_raw(uint32_t object, unsigned id, unsigned previous_raw,
                       unsigned raw, unsigned active, unsigned native_guard,
                       CiEffects *e) {
    if (!e) return CI_BAD_ARGUMENT;
    e->route = CI_PASS_NATIVE;
    e->cancel_prefix = e->deliver_absolute = e->absolute = 0;
    e->index = 0xff;
    if (previous_raw > 255 || raw > 255 || active > 1 || native_guard > 255)
        return CI_BAD_ARGUMENT;
    if (previous_raw == raw) return CI_UNCHANGED;
    e->cancel_prefix = 1;
    if (id > 4 || object != ci_stock_object(id)) return CI_BAD_BINDING;
    if (native_guard) return CI_NATIVE_GUARD;
    if (!id) return CI_CHANGED; /* Keep tempo native, including while GEN is active. */
    e->index = (uint8_t)(id - 1);
    e->absolute = (uint8_t)(raw >> 1);
    e->deliver_absolute = 1;
    e->route = active ? CI_CONSUME : CI_PASS_NATIVE;
    return CI_CHANGED;
}

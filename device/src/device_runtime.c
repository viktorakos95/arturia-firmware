#include "device_runtime.h"
#include "control_input_policy.h"

int ks37_runtime_init(Ks37Runtime *s,uint32_t capacity) {
    if(!s || capacity<sizeof(*s)) return 0;
    uint8_t *bytes=(uint8_t *)s;
    for(unsigned i=0;i<sizeof(*s);++i) bytes[i]=0;
    const uint8_t notes[]={60,64,67};
    const GenParams parameters={.density=5,.rotation=0,.motion=7,.evolve=0,
        .length=32,.accent=55,.range=0,.auto_period=1,.mode=0,.accent_period=3};
    if(!gen_controller_init(&s->controller,notes,3,parameters,37)) return 0;
    bo_init(&s->ownership);ba_init(&s->admission);ih_init(&s->input);
    kl_init(&s->leds);
    /* Playback is uninstalled. pb_init(0) would pretend an installed block:
     * call it only at the eventual verified initial stock-pointer transaction. */
    s->led_profile=KL_PROFILE_UNKNOWN;
    return 1;
}

void ks37_runtime_control(Ks37Runtime *s,uint32_t object,unsigned id,
                         unsigned before,unsigned after,unsigned guard,
                         const GenGestureContext *context,unsigned *route) {
    if(!route) return;
    *route=CI_PASS_NATIVE;
    if(!s) return;
    CiEffects input;
    ci_route_raw(object,id,before,after,s->controller.gesture.active,guard,&input);
    if(input.cancel_prefix) gen_controller_cancel_prefix(&s->controller);
    if(s->faults) {
        /* Do not reassign already-owned GEN controls to native on a fault. */
        *route=input.route;
        return;
    }
    if(input.deliver_absolute) {
        GenControllerEffects effects;
        gen_controller_knob(&s->controller,input.index,input.absolute,context,&effects);
        s->pending_feedback|=effects.flags;
        if(effects.flags&GC_PARAMETER)s->last_control_parameter=effects.parameter;
        if(effects.flags&GC_REQUEST_STOPPED_PUBLICATION) s->stopped_publication_requested=1;
        if(s->observed_controls!=UINT32_MAX) ++s->observed_controls;
    }
    *route=input.route;
}

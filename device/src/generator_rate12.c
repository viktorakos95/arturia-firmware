#include "generator_rate12.h"

void gd_rate_feedback12(GeneratorDeviceRuntime *s,uint32_t object,unsigned id,
                        unsigned before,unsigned after,unsigned native_guard) {
    if(!s || id || object!=0x20000468u || before>255 || after>255 ||
       before==after || native_guard || !s->base.controller.gesture.active ||
       s->base.faults || s->buttons.faults || s->device.phase==PD_FAULT)return;
    /* RATE is the latest serialized UI event. Older undrawn knob/command
     * feedback must not replace it at the following panel snapshot. Musical
     * queues and current/seed snapshots are untouched. */
    const unsigned transient=GC_PARAMETER|GC_QUEUED|GC_APPLIED|GC_REJECTED|GC_CAPTURE_COMPLETE;
    s->base.pending_feedback&=~transient;
    s->feedback_action=0;s->feedback_dirty=0;
    gp_rate_notice12(&s->panel,gd_hw_tick());
}

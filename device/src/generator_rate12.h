#ifndef KS37_GENERATOR_RATE12_H
#define KS37_GENERATOR_RATE12_H
#include "generator_device_runtime.h"

/* Presentation-only RATE notice. The existing parameter byte uses GCP_RATE
 * for RATE; eight generator controls remain0..7. No common struct grows. */
void gp_rate_notice12(GenPanelState *,uint32_t now);
void gp_view_rate12(const GenPanelState *,const GenPanelInput *,GenPanelView *,unsigned bpm);
/* Invoke after the existing control route/controller/feedback processing, in
 * its same saved-mask scope. That processing still owns prefix cancellation.
 * Native RATE stores follow after this callback: never cache an old BPM here. */
void gd_rate_feedback12(GeneratorDeviceRuntime *,uint32_t object,unsigned id,
                        unsigned before,unsigned after,unsigned native_guard);
#endif

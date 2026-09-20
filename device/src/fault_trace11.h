#ifndef KS37_FAULT_TRACE11_H
#define KS37_FAULT_TRACE11_H
#include "generator_device_runtime.h"

/* Diagnostic-only suffix of the owned boot payload. No firmware RAM aliases,
 * allocation, timeout, recovery, native writes or change to error policy. */
typedef struct {
    uint8_t code, site, result, bridge;
    uint32_t prior_flags;
} FaultTrace11;
_Static_assert(sizeof(FaultTrace11)==8,"diagnostic suffix size");

/* Preserve the first fatal cause, before PD overwrites its bridge reason.
 * All helpers preserve the incoming PRIMASK and validate the exact owner. */
void pd_trace_fault11(PlaybackDevice *,unsigned site,unsigned result,unsigned bridge);
void gd_trace_fault11(GeneratorDeviceRuntime *,unsigned code,unsigned result);
/* Captures existing DR/BDA flags only if no earlier trace exists. This is also
 * called before new PD operations so earlier button errors keep precedence. */
unsigned gd_trace_code11(GeneratorDeviceRuntime *);
#endif

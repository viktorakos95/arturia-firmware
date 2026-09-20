#ifndef KS37_CONTROL_INPUT_POLICY_H
#define KS37_CONTROL_INPUT_POLICY_H
#include <stdint.h>

/* Pure route policy for the real raw-change edge at stock08004930.
 * Addresses are compared as numeric evidence bindings, never dereferenced.
 * Caller separately replays the displaced stock strb [r4,+5a], preserves the
 * machine frame, captures a coherent generator/stock-guard context, and routes
 * effects under the same owner. This file is NOT a firmware hook.
 */
enum { CI_PASS_NATIVE, CI_CONSUME };
typedef enum { CI_CHANGED, CI_UNCHANGED, CI_NATIVE_GUARD,
               CI_BAD_ARGUMENT, CI_BAD_BINDING } CiResult;
typedef struct {
    uint8_t route, cancel_prefix, deliver_absolute, index, absolute;
} CiEffects;

/* ID0 is native tempo; ID1..4 are the four generator controls.
 * Unknown returns0. These are RAM object addresses, not global pointer slots. */
uint32_t ci_stock_object(unsigned id);

/* previous_raw=r2 and raw=r3 at4930, both accepted normalized bytes0..255.
 * active is the generator's own0/1 state. native_guard is raw H+0xb9.
 * Every real raw change cancels Chord prefix BEFORE native guard/binding or
 * 7-bit deduplication, including unknown bindings. Nonzero native_guard and
 * unknown bindings always pass stock and never deliver a generator parameter.
 * A valid ID1..4 outside GEN still delivers an observation but passes stock.
 * Caller must process cancel_prefix BEFORE gen_controller_knob(), whose
 * same-absolute early return must not undo physical prefix cancellation.
 * No stock RAM, Scale state, page, bank or CURRENT/SEED is modified here. */
CiResult ci_route_raw(uint32_t object, unsigned id, unsigned previous_raw,
                       unsigned raw, unsigned active, unsigned native_guard,
                       CiEffects *);
#endif

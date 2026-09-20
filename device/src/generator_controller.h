#ifndef KS37_GENERATOR_CONTROLLER_H
#define KS37_GENERATOR_CONTROLLER_H
#include "generator_core.h"
#include "generator_gesture.h"

/* Firmware-port controller glue. Every call is serialized by its hardware
 * owner; no MCU access, clock simulation, IRQ masking or native-context save.
 * The caller supplies canonical note edges and coherent native context.
 * All musical changes remain projected until applied_boundary is called in
 * the SAME transaction as verified stock publication (see playback_bridge).
 */
enum { GCP_LENGTH, GCP_DENSITY, GCP_ROTATION, GCP_EVOLVE,
       GCP_MOTION, GCP_ACCENT, GCP_ACCENT_PERIOD, GCP_AUTO, GCP_RATE, GCP_MODE, GCP_RANGE,
       GC_CONTROLS=8 };
typedef struct {
    GenState core;
    GenGesture gesture;
    GenControl knobs[8];
    uint32_t held_notes[4];
    uint8_t held_count, capture_count, capture[32], pending_restore;
    uint8_t mode_seen, mode_valid;
    uint8_t octave_held, octave_owned;
} GenController;
enum { GC_NATIVE_NOTE=1, GC_NATIVE_KNOB=2, GC_CAPTURE=4, GC_CAPTURE_COMPLETE=8,
       GC_PARAMETER=16, GC_QUEUED=32, GC_REQUEST_STOPPED_PUBLICATION=64,
       GC_REJECTED=128, GC_APPLIED=256 };
enum { GC_QUEUE_FULL=GG_CAPTURE_FULL+1 };
typedef struct {
    GenGestureEffects gesture;
    uint16_t flags;
    uint8_t count, parameter, value, scale_reason, reject_reason;
} GenControllerEffects;

int gen_controller_init(GenController *, const uint8_t *, unsigned, GenParams, uint32_t);
int gen_controller_button(GenController *, unsigned button, unsigned down,
                          const GenGestureContext *, GenControllerEffects *);
int gen_controller_note(GenController *, unsigned pitch, unsigned down,
                        const GenGestureContext *, GenControllerEffects *);
int gen_controller_knob(GenController *, unsigned index, unsigned absolute,
                        const GenGestureContext *, GenControllerEffects *);
enum { GC_OCTAVE_INVALID, GC_OCTAVE_NATIVE, GC_OCTAVE_OWNED };
/* Physical edge, minus=0/plus=1. The DOWN decision owns the complete pair,
 * including release after Shift or GEN changes. BDA separately resolves its
 * native ticket; never interpret OWNED as permission to run a native release. */
unsigned gen_controller_octave(GenController *,unsigned plus,unsigned down,
                               const GenGestureContext *,GenControllerEffects *);
/* Accepted projection, or CURRENT when no queue exists. UI must not recover
 * logical values from raw physical positions or the other page's Scale. */
GenParams gen_controller_parameters(const GenController *);
unsigned gen_controller_parameter_value(const GenController *,unsigned logical);
/* Call after an authentic native Mode poll. First observation/OFF establishes
 * baseline; valid0..7 movement queues one profile,8 is a transition only.
 * Neither this function nor page switching acknowledges native slot fields. */
int gen_controller_mode_observe(GenController *,unsigned detent,
                               const GenGestureContext *,GenControllerEffects *);
/* Called only after the hardware publication protocol has been satisfied.
 * Does NOT itself satisfy that protocol or atomically write stock memory. */
void gen_controller_applied_boundary(GenController *, GenControllerEffects *);
/* Core was already replaced by the exact successfully published proposal.
 * Feedback/optional target restore only; NEVER advances/applies core again. */
void gen_controller_publication_applied(GenController *,GenControllerEffects *);
/* Notify a competing event without inventing a physical button release. */
void gen_controller_cancel_prefix(GenController *);
#endif

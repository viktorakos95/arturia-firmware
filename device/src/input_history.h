#ifndef KS37_INPUT_HISTORY_H
#define KS37_INPUT_HISTORY_H
#include <stdint.h>
#include "generator_gesture.h"

/* Fixed history of accepted input-note obligations, BEFORE USB/DIN source
 * merge. Source values 0/1 match saved subscriber r7; keyboard=2 is assigned by
 * the three scanner wrappers. All calls and effect delivery must be serialized
 * across main/IRQ. This module does not mask IRQs, call stock/controller, touch
 * stock RAM, or prove that wrappers cover every input/reset/drop path.
 */
enum { IH_USB=0, IH_DIN=1, IH_KEYBOARD=2, IH_SOURCE_COUNT=3,
       IH_ALL_SOURCES=7, IH_CAPACITY=128, IH_OMNI=0x7e };
enum { IH_BAD_SOURCE=1, IH_BAD_MESSAGE=2, IH_CAPACITY_LOST=4,
       IH_COUNT_OVERFLOW=8, IH_RESET_UNRESOLVED=16, IH_STREAM_LOST=32,
       IH_REPEATED_ON=64, IH_SCANNER_MISMATCH=128 };
enum { IH_FILTER_CHANGED=1, IH_BASELINE_ACCEPTED=2 };
typedef enum { IH_IGNORED=0, IH_APPLIED, IH_FAULT, IH_INVALID } IhResult;
typedef struct { uint16_t key; uint8_t count, reserved; } IhOwner;
typedef struct {
    IhOwner owners[IH_CAPACITY];
    uint16_t pitch_count[128];
    uint16_t scanner_key_owner[37]; /* (channel<<7|pitch)+1; zero=no observed On. */
    uint32_t epochs[IH_SOURCE_COUNT];
    uint16_t faults[IH_SOURCE_COUNT];
    uint8_t known_sources, seen_sources, held_count, last_filter[2];
} IhState;
typedef struct {
    uint32_t down[4], up[4]; /* Observed canonical union edges, each pitch once. */
    uint16_t faults; /* Newly detected fault, not an implicit cleanup command. */
    uint8_t known_sources, notes_held, flags;
} IhEffects;

/* A verified lifecycle fence, never a timeout or an empty tracker snapshot.
 * Every bit must be supplied by real adapter evidence. HOLDS_RELEASED means
 * all pre-fence accepted holds ended; DRAINED includes partial messages and
 * queued work; COVERAGE includes note/reset/drop entry points.
 */
enum { IH_PROOF_QUIESCED=1, IH_PROOF_DRAINED=2, IH_PROOF_HOLDS_RELEASED=4,
       IH_PROOF_COVERAGE=8, IH_PROOF_SERIALIZED=16, IH_PROOF_ALL=31 };
typedef struct { uint32_t epoch; uint8_t guarantees; } IhBaselineProof;
/* New lifetime only; invalidate old effects/proofs/callbacks before reinit.
 * Zero counts do NOT establish known input history. */
void ih_init(IhState *);
IhResult ih_accept_empty_source(IhState *, unsigned source, IhBaselineProof, IhEffects *);
/* Explicit drops/unknown aliases invalidate coverage, retaining outstanding
 * obligations. No sequence of later Off messages heals sticky UNKNOWN. */
IhResult ih_lose_continuity(IhState *, unsigned source_mask, unsigned reason, IhEffects *);
/* Before native channel filter: positive On is accepted only on matching/omni
 * channel; an Off always pays an EXISTING source/channel/pitch obligation even
 * after filter change. Filter changes never clear old holds. Valid unrelated
 * messages are ignored; channel-mode CC120..127/System Reset invalidate source
 * until a lifecycle proof, rather than claiming a native clear occurred.
 * Repeated On for one tuple retains a count but marks sticky UNKNOWN: the wire
 * does not prove whether it means another obligation or a retrigger.
 */
IhResult ih_receive_midi(IhState *, unsigned source, uint32_t packed, unsigned filter, IhEffects *);
IhResult ih_receive_scanner(IhState *, uint32_t packed, unsigned scanner_key, IhEffects *);
/* Direct adapter ABI for the .S hook callback's captured arguments. */
IhResult ih_receive_hook(IhState *, unsigned source, uint32_t packed,
                         unsigned filter, unsigned scanner_key, IhEffects *);
/* Fills ONLY GG_K_NOTES; never claims that external queues/pending work are idle.
 * Effects may describe observed edges while coverage is unknown. They can keep
 * a mirror up to date but MUST NOT authorize capture/mode entry in that state.
 */
void ih_gesture_notes(const IhState *, GenGestureContext *);
unsigned ih_known_idle(const IhState *);
#endif

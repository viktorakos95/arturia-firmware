#ifndef KS37_GENERATOR_GESTURE_H
#define KS37_GENERATOR_GESTURE_H
#include <stdint.h>

/* Finite ownership router for the existing Chord -> Record contract.
 * Pure, single-owner code; no firmware fields, tick counter or IRQ masking.
 * A hardware caller MUST provide a coherent context, deliver every relevant
 * edge/cancellation, and execute finish-hold/mode effects under the same
 * ownership transaction. The known mask defaults to unknown, never to idle.
 */
enum { GG_SHIFT, GG_RECORD, GG_STOP, GG_PLAY, GG_CHORD, GG_BUTTON_COUNT };
enum { GG_PAGE_CHORD=1, GG_PAGE_CC, GG_PAGE_OTHER };
enum { GG_STOPPED=1, GG_PLAYING, GG_PAUSED };
enum { GG_K_SEQ=1, GG_K_PAGE=2, GG_K_TRANSPORT=4, GG_K_RECORD=8,
       GG_K_PENDING=16, GG_K_NOTES=32, GG_K_LENGTH=64, GG_K_CAPTURE=128,
       GG_K_ALL=255 };
typedef struct {
    uint16_t known;
    uint8_t seq, page, transport, record_idle, pending, notes_held, length_pending, capture;
} GenGestureContext;
enum { GG_OK, GG_SEQ_REQUIRED, GG_PAGE_INELIGIBLE, GG_TRANSPORT_NOT_STOPPED,
       GG_RECORD_BUSY, GG_TRANSITION_PENDING, GG_NOTES_HELD, GG_LENGTH_PENDING,
       GG_CHORD_CAPTURE_ACTIVE, GG_OTHER_BUTTON_HELD, GG_PREFIX_CANCELLED,
       GG_MODE_CHANGED, GG_CAPTURE_ACTIVE, GG_CAPTURE_FULL };
enum { GG_OWNER_NONE, GG_OWNER_NATIVE, GG_OWNER_PREFIX, GG_OWNER_COMBO,
       GG_OWNER_REJECTED, GG_OWNER_COMMAND, GG_OWNER_TRANSPORT, GG_OWNER_RESERVED };
enum { GG_VARY=1, GG_COMMIT, GG_RESET };
enum { GG_HINT_GEN, GG_HINT_UNASSIGNED };
enum { GG_TRANSPORT_STOP, GG_TRANSPORT_TOGGLE };
enum { GG_STARTED_ACTIVE=1, GG_NATIVE_PRESS=2, GG_CANCELLED=4, GG_ATTEMPTED=8 };
typedef struct { uint8_t kind, action, flags, reason; } GenGestureOwner;
typedef struct {
    uint8_t active, held;
    GenGestureOwner owners[GG_BUTTON_COUNT];
} GenGesture;
enum { GG_EFFECT_REARM=1, GG_EFFECT_NATIVE, GG_EFFECT_MODE, GG_EFFECT_COMMAND,
       GG_EFFECT_TRANSPORT, GG_EFFECT_REJECT, GG_EFFECT_HINT, GG_EFFECT_FINISH_HOLD };
typedef struct { uint8_t type, value, button, down; } GenGestureEffect;
enum { GG_MAX_EFFECTS=3 };
typedef struct { uint8_t count; GenGestureEffect items[GG_MAX_EFFECTS]; } GenGestureEffects;

int gen_gesture_init(GenGesture *, unsigned active);
/* Invalid button/down/context-independent arguments return 0, no mutation. */
int gen_gesture_route(GenGesture *, unsigned button, unsigned down,
                      const GenGestureContext *, GenGestureEffects *);
void gen_gesture_cancel_prefix(GenGesture *);
/* Test/UI cleanup only: does NOT clear native holds or silence hardware. */
void gen_gesture_clear_holds(GenGesture *);
#endif

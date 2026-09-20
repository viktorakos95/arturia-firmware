#ifndef KS37_BUTTON_ADMISSION_H
#define KS37_BUTTON_ADMISSION_H
#include "button_ownership.h"
#include "generator_gesture.h"

enum { BA_RECORD_ID=5 };
typedef enum {
    BA_NONE=0, BA_ROUTE_GESTURE, BA_REJECTED_COMBO, BA_CONSUMED_RELEASE, BA_NATIVE_ONLY,
    BA_WAIT_DEBT, BA_INVALID
} BaDecision;
typedef struct {
    uint32_t rejected_record_generation, native_only_chord_generation;
    uint32_t waiting_record_generation, waiting_chord_generation;
    uint8_t rejected_record, native_only_chord, waiting_record;
} BaState;
typedef struct {
    BoRoute route;
    GenGestureEffect feedback; /* Only valid for BA_REJECTED_COMBO. */
} BaRoute;

/* Same lifetime as BoState: never reset while tickets/pairs remain alive. */
void ba_init(BaState *);
/* Sole route-taking entry point for the adapter. Call immediately after each
 * observed edge and after a Chord path acknowledgement, draining available
 * routes before processing the next physical edge; execute router+resolve as
 * one serialized transaction.
 * It is not a general out-of-order event queue. BA_ROUTE_GESTURE means caller must run router and
 * bo_route_resolve. BA_REJECTED_COMBO/BA_CONSUMED_RELEASE already resolve the
 * ticket as SUPPRESS; caller must NOT deliver that edge to the router.
 * BA_NATIVE_ONLY has resolved PASS and must not reach the router: a Chord ack
 * that followed rejected Record cannot create a fresh prefix for that pair.
 * Confirmed Chord with unresolved router DOWN returns BA_WAIT_DEBT for Record
 * without taking its route: caller must finish Chord delivery before retrying.
 * That waiting relation is latched: Chord UP before retry rejects Record;
 * it cannot turn the old combination into a standalone native Record.
 * Rejection is sticky until this Record's physical UP, regardless of Chord UP.
 * This handles only an observed held but unconfirmed native Chord; it neither
 * invents a prefix nor proves all-source context or performs device effects.
 */
BaDecision ba_take_route(BaState *, BoState *, unsigned id, BaRoute *);
#endif

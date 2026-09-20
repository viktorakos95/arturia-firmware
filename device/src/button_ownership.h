#ifndef KS37_BUTTON_OWNERSHIP_H
#define KS37_BUTTON_OWNERSHIP_H
#include <stdint.h>

/* Transaction bookkeeping only. No RAM addresses, native calls, IRQ masking,
 * gesture/controller calls, or assumptions that an emitted effect ran.
 * Real wrappers must carry the returned poll/edge ticket through native pre/post.
 * A mutable global "last edge" is not a substitute for that correlation.
 */
enum { BO_BUTTON_COUNT=9, BO_CHORD_ID=8 };
typedef enum {
    BO_NONE=0, BO_ROUTE_PRESS, BO_ROUTE_RELEASE, BO_PASS_NATIVE,
    BO_SUPPRESS, BO_WAIT_DEBT, BO_INVALID
} BoDecision;
typedef struct { uint32_t generation; uint8_t id, down; } BoTicket;
typedef struct {
    BoDecision decision;
    BoTicket ticket;
    uint8_t native_effect_already_done;
} BoRoute;
typedef struct {
    BoTicket edge;
    uint32_t serial;
    uint8_t native_down;
} BoNativeCall;

enum { BO_DOWN_BIT=1, BO_UP_BIT=2 };
typedef struct {
    uint32_t generation, call_serial;
    uint8_t initialized, physical_down, has_pair, up_seen, blocked, bootstrap_debt;
    uint8_t defer_chord, chord_ack, force_suppress, native_held;
    uint8_t route_taken, route_resolved, native_done, call_done;
    uint8_t down_policy, up_policy;
    uint8_t in_flight, in_flight_edge_down, in_flight_native_down;
} BoButton;
typedef struct { BoButton buttons[BO_BUTTON_COUNT]; } BoState;

/* New lifetime only. Reinitialization requires invalidating ALL old tickets and
 * proving that no old callback can arrive (generation/serial restart at zero). */
void bo_init(BoState *);
/* First stable sample: synchronization only, never ROUTE_PRESS. Neither level
 * proves absence of an inherited native hold or outstanding callback. */
BoDecision bo_sync(BoState *, unsigned id, unsigned down);
/* Before first pair: positive native-idle/no-late-work proof at physical UP.
 * This only acknowledges caller's proof; it does not inspect hardware. */
BoDecision bo_ack_initial_idle(BoState *, unsigned id, unsigned idle_proven);
/* One stable physical transition. defer_chord is allowed only on Chord DOWN
 * outside the generator. It lets stock press execute before routing that DOWN. */
BoDecision bo_observe(BoState *, unsigned id, unsigned down, unsigned defer_chord,
                      BoTicket *out);
BoDecision bo_current_ticket(const BoState *, unsigned id, BoTicket *out);
/* Takes a pending route exactly once. A second take waits for resolution.
 * Chord tentative DOWN is invisible until positive normal-stock-path ack. */
BoDecision bo_take_route(BoState *, unsigned id, BoRoute *out);
/* PASS_NATIVE/SUPPRESS are decisions, not evidence of native execution. */
BoDecision bo_route_resolve(BoState *, BoTicket, BoDecision native_policy);

/* Native handlers use their captured ticket, NOT the router's current owner.
 * WAIT_DEBT is an unresolved transaction: it is neither permission to execute
 * native effects nor permission to skip common stock bookkeeping.
 * pre returns PASS only with a call ticket; post must report actual completion.
 */
BoDecision bo_native_pre(BoState *, BoTicket, unsigned native_down, BoNativeCall *out);
typedef struct {
    uint8_t effects_done; /* Adapter observed the relevant native hold effect. */
    uint8_t normal_chord_path_seen; /* Positive normal-press-path witness. */
    uint8_t chord_d, shift_m, service_mode; /* Post-check for prefix ack. */
} BoNativeProof;
BoDecision bo_native_post(BoState *, BoNativeCall, BoNativeProof);

/* Called only AFTER the adapter really cleared the forwarded Chord hold in an
 * accepted combination's atomic transaction. This transfers remaining native
 * sides to suppression; it does not perform the store or mode change. */
BoDecision bo_ack_finished_chord_hold(BoState *, BoTicket, unsigned observed_d_zero);
/* Explicit recovery proof, not a guessed timeout: caller has established idle
 * native ownership and no late native work for the current pair. Physical UP
 * and no native call in flight are required. Does not erase pending router work. */
BoDecision bo_ack_native_idle(BoState *, BoTicket, unsigned idle_proven);

#endif

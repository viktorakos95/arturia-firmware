#ifndef KS37_PLAYBACK_DEVICE_H
#define KS37_PLAYBACK_DEVICE_H
#include "playback_bridge.h"
#include "stock_sequence.h"

/* Legacy general API: these are obligations of an installed
 * owner, NOT facts inferred by this reader. No default authority exists.
 * All five must remain true for the ENTIRE operation, including stock calls
 * with interrupts restored. In particular a PRIMASK snapshot proves none of
 * input quiescence, lifetime ownership, nor absence of a suspended main frame. */
enum {
    PD_RESERVED_BLOCKS = 1,       /* both 0x408-byte buffers really reserved */
    PD_NATIVE_WRITERS_EXCLUDED = 2, /* S/current/header/builders/pending/record */
    PD_INPUT_TRANSPORT_EXCLUDED = 4, /* new IRQ/USB/DIN/panel events quarantined */
    PD_SERVICE_FRAME_ACCOUNTED = 8, /* enter/leave outside129cc; PRE only its known frame */
    PD_VALID_NATIVE_HEADER = 16, /* saved seven timing/unknown bytes valid */
    PD_REQUIRED = 31
};
typedef enum { PD_OFF, PD_TRANSITION, PD_ACTIVE, PD_FAULT } PdPhase;
typedef enum {
    PD_PASS = 0, PD_OK, PD_PUBLISHED, PD_WAIT, PD_BAD_ARGUMENT,
    PD_NO_AUTHORITY, PD_CONTEXT, PD_STOCK_GUARD, PD_CODEC, PD_INTERLEAVE,
    PD_PUBLICATION_FAULT, PD_STALE_PREPARED, PD_COMMIT_FAULT
} PdResult;
enum { PD_TRANSPORT_PRE, PD_TRANSPORT_POST, PD_STEP_PRE, PD_STEP_POST };
typedef struct {
    PbStock stock;
    uint32_t pending, reload_shadow;
    uint8_t division;
} PdView;
typedef struct {
    PlaybackBridge bridge;
    uint32_t blocks[2], saved_current, saved_pending, epoch;
    uint32_t saved_reload, prepared_serial, completed_cycles, native_phase;
    uint8_t saved_header[8], saved_length, saved_division;
    uint8_t phase, queued, transport_frame, step_frame;
    uint8_t division_requested, requested_division, division_rebase;
    uint8_t native_step_witness, native_wrap, native_step;
} PlaybackDevice;

enum { PD_PREPARE_ENTER = 1, PD_PREPARE_PUBLISH, PD_PREPARE_LEAVE, PD_PREPARE_CYCLE };
typedef struct {
    GenState core, proposed;
    PdView native;
    uint32_t serial, target, cycle_epoch;
    uint8_t kind, ready;
} PdPrepared;
typedef void (*PdCommitFn)(void *context, unsigned kind);

/* Current integration API. Buffers must have been reserved by the caller's
 * linker/boot owner. Only use after original startup and from the proved main
 * handler/service hooks; do not mix this API with the legacy pd_queue/event.
 * No caller-supplied ready/proof flags replace the actual RAM/IRQ guards.
 * Preparation requires thread mode and PRIMASK0; it snapshots core/native
 * briefly under a saved mask, encodes1032bytes outside the critical section,
 * then checks again before marking the ticket ready. Each new preparation
 * supersedes previous tickets. ENTER requires no unapplied core queue.
 */
/* Supported mono NoteOff routing profile, checked before admitting GEN. The
 * caller holds PRIMASK1. This checks exact existing stock pointers/callbacks,
 * normal key-strip view1 (K+952, distinct from U+15 CC/Chord page), chromatic
 * route X+34=0, no chord route U+c==1/U14, hardware
 * output pins. It deliberately does not require N25>0 or queue capacity. */
PdResult pd_entry_profile_locked(PlaybackDevice *);
/* Read-only classification of current stopped-entry refusal; no repairs or
 * permit.0=guards pass,40=context,41=pins,42=idle,43=timing,45=routing;
 * 51..61 identify the stopped fast-guard condition (see implementation).
 * Caller holds PRIMASK1; mask remains unchanged, including wrong context. */
unsigned pd_diagnostic_locked(PlaybackDevice *);
PdResult pd_prepare_enter(PlaybackDevice *, PdPrepared *, const GenState *);
PdResult pd_prepare_publish(PlaybackDevice *, PdPrepared *, const GenState *);
PdResult pd_prepare_leave(PlaybackDevice *, PdPrepared *, const GenState *);
/* Offline private gen_boundary_ex(...,1), for one exact subsequent completed
 * cycle. Never consumed at first Start0, repeated0, stopped or retime0. */
PdResult pd_prepare_cycle(PlaybackDevice *, PdPrepared *, const GenState *);
/* Only the installed12eb6 pre-clear hook supplies this one-use witness.
 * Original C6 and r6 phase are observed before native MOVS/STRB replay. */
void ks37_playback_cycle_observe(uint32_t,unsigned,unsigned,uint32_t);
PdResult pd_step_witness_locked(PlaybackDevice *,uint32_t receiver,unsigned step,
                                unsigned native_wrap,uint32_t native_phase);
/* Requires PRIMASK1 and leaves PRIMASK1 on every path; a wrong incoming mask
 * is restored unchanged. Rechecks live GenState, native pointer/header and
 * real stock guards. cb is mandatory, synchronous and runs after success in
 * this SAME mask. For PUBLISH it must copy prepared.proposed into core and perform feedback/rearm only; ENTER/LEAVE commit mode/ownership only.
 * Callback must preserve PRIMASK and may not recursively call these APIs.
 * Stale preparation never changes CURRENT or stock pointer. A post-STR fault
 * retains block ownership. Caller commits no independent core boundary.
 */
PdResult pd_apply_prepared_locked(PlaybackDevice *, PdPrepared *, GenState *,
                                  PdCommitFn, void *context);
/* Actual transparent stock wrappers dispatch here via the runtime binding.
 * PRE/POST stay paired; PRE may apply a prepared publish at the new step0.
 * Ordinary intervening steps do not invalidate a pending musical ticket.
 * A later Reset/Commit/parameter operation is detected by exact core snapshot
 * comparison, including next_seed and feedback, not just pending_count. */
PdResult pd_event_prepared_locked(PlaybackDevice *, PdPrepared *, GenState *,
                                  unsigned event, uint32_t receiver, unsigned value,
                                  PdCommitFn, void *context);
/* Installed native Time Div witnesses only. Request runs before original
 * 11ee8 stores C49/C54. Applied runs inside the actual12cd2..12cde timer
 * rewrite, with its authentic r5 phase/r6 reload. Both require PRIMASK1 and
 * thread mode; no snapshot alone is accepted as evidence of a legal retime.
 * Timediv remains native. The first subsequent step may jump but cannot be
 * mistaken for the generator's next bar. Prepared publication is revoked.
 */
PdResult pd_time_division_request_locked(PlaybackDevice *, PdPrepared *,
                                       uint32_t receiver,unsigned value);
PdResult pd_time_division_applied_locked(PlaybackDevice *, PdPrepared *,
                                       uint32_t receiver,unsigned value,
                                       uint32_t phase,uint32_t reload);

/* Buffers are provided separately so a future linker-owned Runtime chooses
 * their placement. init neither dereferences nor proves those addresses.
 * Never reinitialize while phase!=OFF, including a fault after publication. */
int pd_init(PlaybackDevice *, uint32_t block0, uint32_t block1);
/* First enter accepts any proven stock slot or working block, length1..64.
 * It preserves that block, pending pointer and header. A real stock129cc call
 * updates cached length AND timer limit after the real current-pointer STR;
 * a real Stop establishes the bridge baseline. This is a stopped musical
 * context save, not a byte-for-byte save of the whole controller/timer state.
 * If an error follows STR, phase remains FAULT and buffers remain owned. */
PdResult pd_enter(PlaybackDevice *, const GenSnapshot *, uint32_t proofs);
PdResult pd_queue(PlaybackDevice *, const GenSnapshot *, uint32_t proofs);
/* Called synchronously by the future wrapper binding. Receiver must be C for
 * transport or N for step. PRE/POST pairs are mandatory even after an error.
 * PD_PUBLISHED requires the caller to commit its matching core projection
 * before returning from the same serialized event. No controller is bound. */
PdResult pd_event(PlaybackDevice *, unsigned event, uint32_t receiver,
                  unsigned value, uint32_t proofs);
PdResult pd_publish_stopped(PlaybackDevice *, uint32_t proofs);
PdResult pd_leave(PlaybackDevice *, uint32_t proofs);
/* Separately validated, non-waiting stopped subset. These replace the two
 * exclusion assertions with a real uninterrupted PRIMASK transaction.
 * Required caller facts are RESERVED_BLOCKS, SERVICE_FRAME_ACCOUNTED and
 * VALID_NATIVE_HEADER only. Read guards require N25/27/28=0, clock queue empty,
 * C24!=0, singleton160 present, H5c bit1 clear, and timer stopped/counter0.
 * No NoteOff I/O is attempted for a nonzero N25; ownership is retained. */
PdResult pd_enter_masked(PlaybackDevice *, const GenSnapshot *, uint32_t proofs);
PdResult pd_leave_masked(PlaybackDevice *, uint32_t proofs);

/* Platform binding below is REAL ARM RAM access/stock calls in
 * playback_device_mmio.c. Tests provide deterministic substitutes. */
uint32_t pd_hw_ipsr(void);
uint32_t pd_hw_control(void);
uint32_t pd_hw_irq_save(void);
void pd_hw_irq_restore(uint32_t);
uint8_t pd_hw_read8(uint32_t);
uint32_t pd_hw_read32(uint32_t);
uint8_t *pd_hw_block(uint32_t);
/* Bounded native-profile CEN hold for a length-changing publication. Exactly
 * S/C10/ARR/shadow are committed, then original CR1 restored. No CNT/UG/BC.
 * PD_STOCK_GUARD=no effects; PD_WAIT=no swap, CR1 restored; PD_PUBLISHED=swap
 * with restored CR1; PD_PUBLICATION_FAULT=partial or unproved hardware state,
 * retain ownership even if failure preceded the pointer store. */
PdResult pd_hw_publish_length(uint32_t next,unsigned length,uint32_t reload,
                              unsigned ticks,unsigned running);
void pd_hw_store_current(uint32_t); /* aligned S+0 STR, caller holds PRIMASK */
void pd_hw_cleanup(void);         /* actual14258(N); general: IRQs restored;
                                    prepared: guarded bounded mono release */
void pd_hw_service(void);         /* actual129cc(C); general: IRQs restored,
                                    masked subset: validated bounded calls */
void pd_hw_stop(void);            /* actual unpatched12334(C,0) trampoline */
uint32_t pd_hw_timer_word(unsigned offset); /* existing timer record200054bc */

/* Runtime supplies these callbacks. Admitted calls execute native stock once;
 * unsupported events must latch faults and keep ownership. These hooks alone
 * do not isolate the other stock sequence writers. */
/* Whole transport admission is required before the native body. Return0 to
 * preserve original args/APSR and return with no PRE/native/POST calls.
 * Runtime permits only OFF or its exact main transport dispatch while owned. */
unsigned ks37_playback_allow(uint32_t receiver, unsigned action);
void ks37_playback_event(unsigned event, uint32_t receiver, unsigned value);
#endif

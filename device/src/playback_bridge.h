#ifndef KS37_PLAYBACK_BRIDGE_H
#define KS37_PLAYBACK_BRIDGE_H
#include <stdint.h>

/* Publication POLICY, not installed hooks. All APIs are single-owner and must
 * be serialized with commands and EVERY stock writer/transport callback.
 * Raw snapshots do not establish that exclusion. There are no MCU accesses,
 * IRQ masks, allocations, inferred DMA-ready flags, or pointer stores here.
 * Current scope: already-installed variable-length generator blocks, SEQ, C+c==0.
 */
typedef struct {
    uint32_t current_block;       /* [S+0], S=20002c4c */
    int32_t selected_step;        /* [C+38], C=20002bec */
    int32_t release_deadline;     /* [C+44] */
    int32_t phase_target;         /* [C+58] */
    uint8_t ui_mode, ui_transport;/* U+0f,+10; U=*[20001124] */
    uint8_t emit, paused, selector, cached_length; /* C+4,+5,+c,+10 */
    uint8_t target_11, reset_request, reset_target; /* C+11,+12,+13 */
    uint8_t retrigger_request, retrigger_target; /* C+54,+55 */
    uint8_t switch_request, switch_target; /* C+56,+57 */
    uint8_t rebuild_request, rebuild_target; /* C+5d,+5e */
    uint8_t note_release, note_suppressed, note_retain; /* N+25,+27,+28 */
    uint8_t header[8];            /* current block +400..407 */
} PbStock;

typedef enum {
    PB_OK, PB_WAIT, PB_PUBLISH_STOPPED, PB_PUBLISH_ZERO, PB_NEED_STOCK_CLEANUP,
    PB_BAD_ARGUMENT, PB_GUARD_FAILED, PB_DESYNC, PB_INTERLEAVED,
    PB_REPHASE_UNSUPPORTED, PB_HEADER_CHANGED, PB_STALE_PERMIT
} PbResult;
typedef enum { PB_UNKNOWN, PB_STOPPED, PB_RUNNING, PB_PAUSED } PbTransport;
typedef struct {
    uint32_t current_block, serial;
    uint8_t transport, have_step, last_step, step_active, zero_window, fault, length, cycle_window;
} PlaybackBridge;
typedef struct {
    uint32_t serial, previous_block, next_block;
    uint8_t kind, header[8], previous_length;
} PbPermit;

/* No policy state may be inferred merely from an initial stock snapshot.
 * Initialization starts a new object lifetime: caller must discard ALL old
 * permits before calling it, including reinitializing the same storage. */
PbResult pb_init(PlaybackBridge *, uint32_t installed_block, unsigned length);

/* Emit after the corresponding stock 12334(C,action) has returned. Actions
 * 0=Stop,1=Start,2=Pause,3=Continue,4=rephase. U+10 may still be old here:
 * stopped publication separately waits for U+10==0. */
PbResult pb_transport_after(PlaybackBridge *, unsigned action, const PbStock *);

/* Mandatory matched PRE/POST for the actual 12ec0 call, not main-loop polling.
 * PRE requires C+38==step; POST closes its zero window. */
PbResult pb_step_before(PlaybackBridge *, unsigned step, const PbStock *);
/* native_wrap comes only from the actual pre-clear C6 witness. In particular
 * repeated step0 at L1 without that witness is not a completed cycle. */
PbResult pb_step_before_cycle(PlaybackBridge *,unsigned,const PbStock *,unsigned native_wrap);
PbResult pb_step_after(PlaybackBridge *);

/* Sole exceptional step transition: caller has observed the actual native
 * Time Div timer rewrite since the preceding step, with owned block/header
 * unchanged. Accept its first real step as a new baseline, never a bar-zero
 * publication window. It does not touch stock phase, notes or transport. */
PbResult pb_time_division_step(PlaybackBridge *, unsigned step, const PbStock *);

/* Known stock writer/context change outside the supported events revokes
 * outstanding permits and requires a later real Stop to establish a baseline. */
PbResult pb_external_change(PlaybackBridge *);

/* A permit denotes a CURRENT boundary only, never stock pending_block.
 * Must prepare next_block off-line first, with identical header bytes1..7 and a validated length1..64 and
 * stock_sequence codec (no ties). Caller passes the actually encoded header.
 * NEED_STOCK_CLEANUP requests stock14258, then a fresh snapshot + offer retry;
 * it does not call cleanup or fabricate completion. Every offer attempt
 * supersedes any earlier permit, including on guard failure. */
PbResult pb_offer(PlaybackBridge *, const PbStock *, uint32_t next_block,
                  const uint8_t next_header[8], PbPermit *);

/* After caller has performed its independently-proven publication protocol,
 * acknowledge by freshly observing S.current==permit.next_block. This updates
 * only policy bookkeeping. It neither performs nor proves a hardware swap.
 * Any intervening API event/offer invalidates the permit; every observe attempt
 * consumes it, including failures. Generator gen_boundary belongs
 * in the SAME serialized transaction; do not call it merely on pb_offer. */
PbResult pb_observe_swap(PlaybackBridge *, const PbStock *, const PbPermit *);
#endif

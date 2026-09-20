#ifndef KS37_NATIVE_ADMISSION_H
#define KS37_NATIVE_ADMISSION_H

#include <stdint.h>
#include "generator_gesture.h"

/* Proven stock 1.1.6.579 layout, NOT a complete MCU/native state model.
 * Caller resolves U/C pointers and holds a suitable critical section for read.
 * A zero local_rejects result is NOT permission to switch mode: event history,
 * all note sources, native consumer, and queued/IRQ inputs are separate gates.
 */
typedef struct {
    uint8_t u_published_record, u_mode_request, u_record_request;
    uint8_t u_record_suppression, u_chord_mode, u_chord_held, u_seq;
    uint8_t u_transport, u_record, u_start_wait, u_page, u_transport_aux;
    uint32_t u_page_timer;
    uint8_t c_emit, c_pause, c_clock_selector;
    uint8_t c_length_target, c_rephase_request, c_rephase_target;
    uint8_t c_clock_start_arm, c_clock_resume_arm;
    uint8_t c_division_request, c_division_target;
    uint8_t c_block_request, c_block_target;
    uint8_t c_rebuild_request, c_rebuild_target;
    uint32_t c_rephase_threshold, c_release_deadline;
    uint8_t service_mode, record_held, length_pending, capture_started, shift;
} Ks37NativeSnapshot;

typedef struct {
    const volatile uint8_t *u; /* at least 0x1d bytes */
    const volatile uint8_t *c; /* at least 0x5f bytes */
    const volatile uint8_t *h; /* at least 0xba bytes */
    const volatile uint8_t *record_held;    /* 0x200010b6 */
    const volatile uint8_t *length_pending; /* 0x200010c2 */
    const volatile uint8_t *capture_started; /* 0x200010e4 */
    const volatile uint8_t *shift;          /* 0x200010d2 */
} Ks37NativeSources;

enum {
    KS37_REJECT_SEQ = 1u << 0,
    KS37_REJECT_PAGE = 1u << 1,
    KS37_REJECT_TRANSPORT = 1u << 2,
    KS37_REJECT_RECORD = 1u << 3,
    KS37_REJECT_LENGTH = 1u << 4,
    KS37_REJECT_CAPTURE = 1u << 5,
    KS37_REJECT_UI_PENDING = 1u << 6,
    KS37_REJECT_CONTROLLER_PENDING = 1u << 7,
    KS37_REJECT_RELEASE_PENDING = 1u << 8,
    KS37_REJECT_SERVICE = 1u << 9,
    KS37_REJECT_SHIFT = 1u << 10
};

void ks37_native_snapshot_read(Ks37NativeSnapshot *out, const Ks37NativeSources *sources);
uint32_t ks37_native_local_rejects(const Ks37NativeSnapshot *snapshot);
/* Produces a partial fail-closed context for the shared gesture router.
 * NOTES always remains unknown. PENDING is known only when a local blocker
 * exists; local silence alone never establishes global pending == false. */
void ks37_native_gesture_context(GenGestureContext *out, const Ks37NativeSnapshot *snapshot);

enum {
    KS37_READ_MASKABLE_IRQS_HELD = 1u << 0,
    KS37_NEED_INPUT_HISTORY = 1u << 0,
    KS37_NEED_EXTERNAL_PENDING = 1u << 1,
    KS37_NEED_ATOMIC_TRANSITION = 1u << 2
};
typedef struct {
    Ks37NativeSnapshot snapshot;
    uint32_t read_evidence, unresolved;
} Ks37NativeDeviceRead;
/* ARM/Thumb-only reader, never call on host. Requires privileged Thread mode.
 * Saves PRIMASK, masks IRQs, validates known U/C pointers, reads, restores the
 * ORIGINAL mask. It does NOT keep a transaction open after return. */
int ks37_native_read_device(Ks37NativeDeviceRead *out);

/* Read-only observer after stock debounce 0x0801a760, called through the BL
 * at 0x0801a7c2. released_sample is that call's original r1 (raw sample == 0).
 * First stable sample only synchronizes; it must NOT execute a user command.
 * Unstable samples keep last stable held state. No stock B fields are written.
 */
typedef struct { uint16_t seen, held; } Ks37ButtonObserver;
typedef enum {
    KS37_BUTTON_NONE = 0, KS37_BUTTON_INITIAL_UP, KS37_BUTTON_INITIAL_DOWN,
    KS37_BUTTON_UP, KS37_BUTTON_DOWN, KS37_BUTTON_INVALID
} Ks37ButtonObservation;
Ks37ButtonObservation ks37_button_observe(Ks37ButtonObserver *observer,
                                         const volatile uint8_t *button,
                                         unsigned released_sample);

#endif

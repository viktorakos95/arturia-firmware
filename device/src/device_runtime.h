#ifndef KS37_DEVICE_RUNTIME_H
#define KS37_DEVICE_RUNTIME_H
#include "generator_controller.h"
#include "button_admission.h"
#include "native_admission.h"
#include "playback_bridge.h"
#include "stock_sequence.h"
#include "input_history.h"
#include "keyled_adapter.h"

/* All state is explicitly owned by the extended Reset BSS. No guessed holes,
 * malloc, copied firmware globals or pointers into a transient stack frame.
 * Hooks serialize every mutation. This is not permission to activate GEN:
 * input baseline, native button effects and first publication remain required.
 */
enum { DR_BOOT_FAULT=1, DR_INPUT_FAULT=2, DR_CONTEXT_FAULT=4, DR_LED_FAULT=8 };
typedef struct {
    GenController controller;
    BoState ownership;
    BaState admission;
    Ks37ButtonObserver observer;
    PlaybackBridge playback;
    IhState input;
    KLState leds;
    KLOverlay overlay;
    uint8_t sequence[2][STOCK_SEQUENCE_BYTES];
    uint32_t faults, observed_controls, observed_notes;
    uint16_t pending_feedback;
    uint8_t stopped_publication_requested, led_profile, last_control_parameter;
} Ks37Runtime;

/* Pure initialization called by the strong target boot callback. Defaults
 * match the isolated prototype/v2/core.mjs; they do not replace a stock sequence. */
int ks37_runtime_init(Ks37Runtime *, uint32_t capacity);
/* Effects are coalesced only where they describe level-triggered requests.
 * No gesture/native effects may be put here: those need synchronous handling. */
void ks37_runtime_control(Ks37Runtime *, uint32_t object, unsigned id,
                         unsigned before, unsigned after, unsigned guard,
                         const GenGestureContext *, unsigned *route);

/* ARM-only entry points called by the actual Thumb wrappers. */
unsigned ks37_control_input_entry(uint32_t object, uint32_t id,
                                  uint32_t before, uint32_t after);
void ks37_note_input_entry(uint32_t source, uint32_t packed,
                           uint32_t filter, uint32_t scanner_key);
unsigned ks37_keyled_entry(uint32_t receiver);
#endif

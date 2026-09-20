#ifndef KS37_BUTTON_DEVICE_ADAPTER_H
#define KS37_BUTTON_DEVICE_ADAPTER_H
#include "button_admission.h"
#include "generator_controller.h"
#include "native_admission.h"

/* Separate, uninstalled adapter. No existing runtime/header/build is changed.
 * A binding starts before stock constructors. Reinit requires a fresh boot;
 * neither a zero RAM sample nor bda_init acknowledges native button idle. */
enum { BDA_MAGIC=0x42445031u, BDA_PASS=0, BDA_CONSUME=1 };
enum { BDA_BAD_LIFETIME=1, BDA_BAD_CALL=2, BDA_ROUTE_ERROR=4,
       BDA_MODE_PROTOCOL=8, BDA_EXTERNAL_FAULT=16 };
enum { BDA_HAVE_TICKET=1, BDA_BLOCK_RECORD=2, BDA_STABLE_UP=4,
       BDA_CALL_OPEN=8, BDA_UNTRACKED=16, BDA_BLOCK_DEBT=32, BDA_CALL_POISON=64 };
enum { BDA_FIELD_U=1, BDA_FIELD_GLOBALS=2, BDA_FIELDS_ALL=3 };
typedef struct { uint8_t known,d,record,shift,service,octave_minus,octave_plus; } BdaFields;
/* The actual observer wrapper reserves exactly64 bytes, at copied poll SP+16.
 * No pointers, compiler packing or mutable global last-edge slot in this ABI. */
typedef struct {
    uint32_t magic,object,serial;
    BoTicket ticket;
    BoNativeCall call;
    uint32_t flags,witness,id,native_down,reserved[3];
} BdaPoll;
typedef struct {
    uint32_t serial,faults,objects[BO_BUTTON_COUNT];
    uint16_t constructors;
    uint8_t bootstrap_record_suppressed;
} BdaState;
typedef struct {
    BoTicket ticket;
    GenControllerEffects effects;
    uint8_t admission,native_policy,native_effect_already_done;
    uint8_t mode_status,mode_receipts;
} BdaDelivery;
enum { BDA_TX_REFUSED=0, BDA_TX_COMMITTED=1, BDA_TX_FAULT_AFTER_PUBLICATION=2,
       BDA_TX_STATUS_MASK=3, BDA_TX_D_ZERO=4 };
typedef struct {
    BoTicket ticket,chord_ticket;
    GenGestureContext context;
    const GenController *before_controller;
    const BoState *before_ownership;
    /* Only controller may be updated, for example applied_boundary after a
     * prepared pointer publication. Preserve its gesture/physical ownership.
     * Do not mutate live binding state or the read-only ownership proposal. */
    GenController *controller;
    const BoState *ownership;
    const GenControllerEffects *effects;
    uint8_t button,down,old_active,new_active,finish_hold;
    uint8_t native_chord_down_already_done;
} BdaModeTransaction;
typedef unsigned (*BdaModeTransactionFn)(void *,BdaModeTransaction *);
typedef struct {
    BdaState *state;
    BoState *ownership;
    BaState *admission;
    Ks37ButtonObserver *observer;
    GenController *controller;
    void *user;
    void (*context)(void *,GenGestureContext *);
    /* Synchronous diagnostic/feedback delivery. NEVER execute GG_EFFECT_NATIVE
     * here: native_policy controls the real handler and an acknowledged Chord
     * DOWN already ran. MODE/FINISH_HOLD were already committed by transaction;
     * delivery must not execute them a second time. */
    void (*deliver)(void *,const BdaDelivery *);
    /* Called only for a proposed mode change, under the SAME caller critical
     * section as context/read/route and final software commit. Bulk preparation
     * belongs before this call; apply a prepared locked publication here.
     * REFUSED requires NO external changes. COMMITTED is returned only after
     * all external changes completed, with actual D_ZERO receipt if finish_hold.
     * A possibly-performed pointer store followed by failure returns
     * FAULT_AFTER_PUBLICATION (plus D_ZERO only if actually observed).
     * BDA latches fault, retains the old controller, suppresses Record/native
     * continuation, and NEVER releases any external buffer on that outcome.
     * Root/runtime must retain publication ownership and block its other writers
     * independently of gesture.active. Missing callback refuses mode changes.
     * This callback cannot reenter BDA/controller or dispatch new physical edges. */
    BdaModeTransactionFn mode_transaction;
} BdaBinding;

void bda_init(BdaState *);
/* Called from actual constructor exit, before first poll of this object. */
void bda_constructed(BdaBinding *,uint32_t object,unsigned id);
void bda_observed(BdaBinding *,BdaPoll *,uint32_t object,unsigned id,
                  Ks37ButtonObservation,unsigned stable_up);
unsigned bda_native_pre(BdaBinding *,BdaPoll *,uint32_t object,unsigned down);
void bda_path(BdaBinding *,BdaPoll *,uint32_t object,unsigned down);
void bda_native_post(BdaBinding *,BdaPoll *,uint32_t object,unsigned down,BdaFields);
/* Actual poll has returned from all synchronous handlers. Quiet button bytes
 * come from this SAME object after poll; fields are sampled in the same IRQ-
 * masked callback. This establishes only physical-handler bootstrap idle,
 * never all-source pending/notes or an empty musical queue. */
void bda_poll_end(BdaBinding *,BdaPoll *,const volatile uint8_t *button,BdaFields);

/* ARM entries used by button_device_hooks.S. Future runtime supplies a strong
 * binding getter; there is no default ready state or guessed RAM allocation.
 * All entries save/restore PRIMASK. Binding callbacks must not reenter poll or
 * dispatch asynchronous effects; observer/path-ack routes are drained inline. */
BdaBinding *ks37_button_device_binding(void);
void ks37_button_constructor_entry(uint32_t object,unsigned id);
void ks37_button_poll_entry(BdaPoll *,uint32_t object,unsigned released_sample);
unsigned ks37_button_native_pre_entry(BdaPoll *,uint32_t object,unsigned down);
void ks37_button_native_post_entry(BdaPoll *,uint32_t object,unsigned down);
void ks37_button_path_entry(BdaPoll *,uint32_t u,unsigned down);
void ks37_button_poll_end_entry(BdaPoll *);
#endif

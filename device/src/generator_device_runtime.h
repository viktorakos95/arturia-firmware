#ifndef KS37_GENERATOR_DEVICE_RUNTIME_H
#define KS37_GENERATOR_DEVICE_RUNTIME_H
#include "device_runtime.h"
#include "button_device_adapter.h"
#include "playback_device.h"
#include "input_device.h"
#include "generator_panel.h"
#include "generator_keyled.h"

enum { GD_PUBLICATION_FAULT=16, GD_NATIVE_FAULT=32, GD_TRANSPORT_OVERFLOW=64,
       GD_BAD_PROFILE=128, GD_PREPARE_FAULT=256 };
enum { GD_TRANSPORT_CAPACITY=8 };
typedef struct {
    Ks37Runtime base;
    BdaState buttons;
    BdaBinding button_binding;
    PlaybackDevice device;
    PdPrepared prepared;
    InputDeviceState inputs;
    GenPanelState panel;
    GenKeyledState keyled;
    uint32_t mode_changes, publications, refused, quarantined, main_frames;
    uint32_t cpuid, feedback_tick, feedback_action, last_parameter;
    uint8_t transport[GD_TRANSPORT_CAPACITY], transport_count;
    uint8_t transport_permit, mode_d_zero, main_open, feedback_dirty;
    uint8_t fault_stop_attempted;
    uint8_t mode_diagnostic;
} GeneratorDeviceRuntime;

int gd_init(GeneratorDeviceRuntime *,uint32_t capacity);
int gd_owns_native(const GeneratorDeviceRuntime *);
void gd_input_effects(void *,const IhEffects *);
void gd_context(void *,GenGestureContext *);
void gd_delivery(void *,const BdaDelivery *);
void gd_control_feedback(GeneratorDeviceRuntime *,unsigned flags,unsigned parameter);
unsigned gd_mode_transaction(void *,BdaModeTransaction *);
void gd_publication_commit(void *,unsigned kind);
void gd_main_before(GeneratorDeviceRuntime *);
void gd_main_after(GeneratorDeviceRuntime *);
void gd_native_event(GeneratorDeviceRuntime *,unsigned,uint32_t,unsigned);

/* Target binding. Context/edges are serialized by caller; preparation alone
 * runs outside the mask. No synthetic proof bits close input/pending state. */
int gd_hw_context(GeneratorDeviceRuntime *,GenGestureContext *);
int gd_hw_finish_chord(void);
void gd_hw_transport(GeneratorDeviceRuntime *,unsigned request);
void gd_hw_feedback(GeneratorDeviceRuntime *);
uint32_t gd_hw_cpuid(void);
uint32_t gd_hw_tick(void);
unsigned gd_hw_mode_detent(void);
#endif

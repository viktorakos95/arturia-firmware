#ifndef KS37_GENERATOR_DIAGNOSTICS_H
#define KS37_GENERATOR_DIAGNOSTICS_H
#include "generator_device_runtime.h"

/* Values 1..14 retain the gesture/controller rejection reason. These are
 * observations of the existing gates, never permission or recovery actions. */
enum {
    GD_DIAG_UI_PENDING=21, GD_DIAG_CONTROLLER_PENDING=22,
    GD_DIAG_RELEASE_PENDING=23, GD_DIAG_SERVICE=24, GD_DIAG_SHIFT=25,
    GD_DIAG_QUEUES=26, GD_DIAG_NO_COLD=27, GD_DIAG_INPUT_LOSS=28,
    GD_DIAG_HISTORY_UNKNOWN=29, GD_DIAG_PHYSICAL_UNKNOWN=30,
    GD_DIAG_PHYSICAL_HELD=31, GD_DIAG_HISTORY_HELD=32,
    GD_DIAG_BOOT_OR_BASE=33, GD_DIAG_BUTTON_FAULT=34,
    GD_DIAG_TRANSPORT_PENDING=35, GD_DIAG_ENTRY_PROFILE=45,
    GD_DIAG_UNCLASSIFIED=99
};

/* Privileged Thread caller holds PRIMASK1 for the entire observation. This
 * reads the published runtime and current native state, without probing boot
 * health, modifying baselines, consuming tickets, or clearing any fault.
 * Faults 33/34 take precedence. Otherwise only TRANSITION_PENDING and
 * NOTES_HELD are refined; the other valid original reasons remain unchanged.
 * The native reader/profile API preserve the incoming mask. */
unsigned gd_diagnostic_reason(GeneratorDeviceRuntime *, unsigned gesture_reason);
#endif

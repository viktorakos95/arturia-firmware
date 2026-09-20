#include "generator_diagnostics.h"
#include "boot_ram.h"

unsigned gd_diagnostic_reason(GeneratorDeviceRuntime *s,unsigned reason) {
    const volatile BootRam *boot=boot_ram_status_get();
    if(!s||!boot||boot->phase!=BOOT_READY||boot->faults||
       boot->published!=1||s->base.faults)return GD_DIAG_BOOT_OR_BASE;
    if(s->buttons.faults)return GD_DIAG_BUTTON_FAULT;
    if(reason!=GG_TRANSITION_PENDING&&reason!=GG_NOTES_HELD)
        return reason>=1&&reason<=GC_QUEUE_FULL?reason:GD_DIAG_UNCLASSIFIED;

    if(s->transport_count||s->transport_permit)return GD_DIAG_TRANSPORT_PENDING;
    if(!s->inputs.cold_used)return GD_DIAG_NO_COLD;
    if(s->inputs.lost_sources)return GD_DIAG_INPUT_LOSS;
    if(s->base.input.known_sources!=IH_ALL_SOURCES)return GD_DIAG_HISTORY_UNKNOWN;
    if(!input_device_physical_known(&s->inputs,s->inputs.frame_epoch))
        return GD_DIAG_PHYSICAL_UNKNOWN;
    if(!input_device_physical_empty(&s->inputs,s->inputs.frame_epoch))
        return GD_DIAG_PHYSICAL_HELD;
    if(s->base.input.held_count)return GD_DIAG_HISTORY_HELD;

    Ks37NativeDeviceRead native;
    /* No diagnostic code claims that a failed native read was a good context.
     * Keep the router's existing refusal if no safe snapshot is available. */
    if(!ks37_native_read_device(&native))return reason;
    uint32_t rejects=ks37_native_local_rejects(&native.snapshot);
    if(rejects&KS37_REJECT_UI_PENDING)return GD_DIAG_UI_PENDING;
    if(rejects&KS37_REJECT_CONTROLLER_PENDING)return GD_DIAG_CONTROLLER_PENDING;
    if(rejects&KS37_REJECT_RELEASE_PENDING)return GD_DIAG_RELEASE_PENDING;
    if(rejects&KS37_REJECT_SERVICE)return GD_DIAG_SERVICE;
    if(rejects&KS37_REJECT_SHIFT)return GD_DIAG_SHIFT;

    /* Exactly the queue/object predicates in gd_hw_context. Short-circuit
     * on each changed pinned object before reading its queue fields. */
    if(pd_hw_read32(0x200010e0u)!=0x200023f8u||
       pd_hw_read32(0x20002538u)!=pd_hw_read32(0x2000253cu)||
       pd_hw_read32(0x20002728u)!=pd_hw_read32(0x2000272cu)||
       pd_hw_read32(0x200010d8u)!=0x20004f18u||
       pd_hw_read8(0x2000509eu)!=pd_hw_read8(0x2000509fu))
        return GD_DIAG_QUEUES;
    if(pd_entry_profile_locked(&s->device)!=PD_OK)return GD_DIAG_ENTRY_PROFILE;
    return reason;
}

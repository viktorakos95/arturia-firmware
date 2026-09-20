.syntax unified
.cpu cortex-m3
.thumb
.text

/* Every saved callback frame is32 bytes and retains incoming 8-byte alignment. */
.macro save_callback
    push {r0-r3,r12,lr}
    mrs r12,apsr
    sub sp,sp,#8
    str r12,[sp]
.endm
.macro restore_callback
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add sp,sp,#8
    pop {r0-r3,r12,lr}
.endm
.macro function name
    .global \name
    .type \name,%function
    .thumb_func
\name:
.endm

/* B.W at154d0. Publish exactly once under the SAME outer PRIMASK transaction
 * as the baseline callback. Displaced literal LDR's final r3 is restored. */
function ks37_input_cold_hook
    save_callback
    mrs r12,primask
    str r12,[sp,#4]
    cpsid i
    mov r0,r5
    bl ks37_input_cold_entry
    ldr r12,=0x200010e0
    str r5,[r12]
    ldr r12,[sp,#4]
    msr primask,r12
    restore_callback
    ldr r3,=0x200010e0
    ldr pc,=0x080154d5
.size ks37_input_cold_hook,.-ks37_input_cold_hook

/* B.W+NOP at1862a. The status register is read ONCE, before stock DR read.
 * Callback never rereads SR or DR. Exact TST flags replace callback flags. */
function ks37_input_uart_status_hook
    ldr r2,[r3]
    save_callback
    mov r0,r3
    mov r1,r2
    bl ks37_input_uart_status_entry
    restore_callback
    tst r2,#0x20
    ldr pc,=0x08018631
.size ks37_input_uart_status_hook,.-ks37_input_uart_status_hook

/* B.W atc05e. Implements the entire original six-byte getter: one LDRH+BX. */
function ks37_input_usb_flags_hook
    save_callback
    ldrh r1,[r0,#0x44]
    str r1,[sp,#8] /* returned r0, original r1 stays saved */
    bl ks37_input_usb_flags_entry
    restore_callback
    bx lr
.size ks37_input_usb_flags_hook,.-ks37_input_usb_flags_hook

/* B.W at e488; original prologue is replayed after observing deinit. */
function ks37_input_usb_deinit_hook
    save_callback
    bl ks37_input_usb_deinit_entry
    restore_callback
    push {r4,lr}
    mov r4,r0
    ldr pc,=0x0800e48d
.size ks37_input_usb_deinit_hook,.-ks37_input_usb_deinit_hook

/* BL at150a4. The target C wrapper calls stock10638 once with original args,
 * returns its r0 and obeys normal AAPCS callee-save/stack rules. */
function ks37_input_usb_ingress_hook
    b ks37_input_usb_ingress_entry
.size ks37_input_usb_ingress_hook,.-ks37_input_usb_ingress_hook

/* BL at157a8. Outer8 preserves original argument and return address.
 * Post-callback restores ALL caller registers/flags as returned by stock. */
function ks37_input_acquire_hook
    sub sp,sp,#8
    str r0,[sp]
    str lr,[sp,#4]
    save_callback
    bl ks37_input_acquire_begin_entry
    restore_callback
    bl .Lcall_acquisition
    save_callback
    ldr r0,[sp,#32]
    bl ks37_input_acquire_end_entry
    restore_callback
    ldr lr,[sp,#4]
    add sp,sp,#8
    bx lr
.size ks37_input_acquire_hook,.-ks37_input_acquire_hook

function ks37_input_scan_hook
    sub sp,sp,#8
    str r0,[sp]
    str lr,[sp,#4]
    bl .Lcall_scanner
    save_callback
    ldr r0,[sp,#32]
    bl ks37_input_scan_end_entry
    restore_callback
    ldr lr,[sp,#4]
    add sp,sp,#8
    bx lr
.size ks37_input_scan_hook,.-ks37_input_scan_hook
.Lcall_acquisition:
    ldr pc,=0x08019139
.Lcall_scanner:
    ldr pc,=0x0800cc49
.ltorg

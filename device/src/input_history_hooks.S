.syntax unified
.cpu cortex-m3
.thumb
.text

/* Integration must define this callback. No default no-op/ready state exists.
 * void ks37_note_input_entry(unsigned source, uint32_t message,
 *                                 unsigned input_filter, unsigned key_index);
 * source: USB=0/DIN=1 from original subscriber r7, scanner=2 from caller.
 * Callback must serialize its private state across main/IRQ, preserve normal
 * AAPCS callee-saved registers and execute no invented native cleanup.
 */
.extern ks37_note_input_entry

/* Replace [08010310,08010316) with B.W here plus NOP. Entered by branch, not BL:
 * r0=stock input-channel result, r4=M, r5=message pointer, r7=original source.
 * Original callback's stack frame is56 bytes. Save32 more, retaining alignment.
 */
.global ks37_input_midi_pre_filter_hook
.type ks37_input_midi_pre_filter_hook,%function
.thumb_func
ks37_input_midi_pre_filter_hook:
    push {r0-r3,r12,lr}
    mrs r12,apsr
    sub sp,sp,#8
    str r12,[sp]
    mov r2,r0
    mov r0,r7
    ldr r1,[r5]
    movs r3,#0
    mvns r3,r3
    bl ks37_note_input_entry
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add sp,sp,#8
    pop {r0-r3,r12,lr}
    /* Exact effects of displaced stock instructions. */
    ldrb r6,[r5]
    and r6,r6,#0x0f
    ldr pc, .Lmidi_resume
.size ks37_input_midi_pre_filter_hook,.-ks37_input_midi_pre_filter_hook
.p2align 2
.Lmidi_resume: .word 0x08010317

/* Replace the BL at0800cdfa/0800cf30/0800d064 with BL here. All three have
 * r1=actual packed message and r5=scanner key index0..36. Scanner frame88 bytes.
 * Original r0/r1/r2/r3/LR restored; tail-call executes original input exactly once.
 */
.global ks37_input_scanner_hook
.type ks37_input_scanner_hook,%function
.thumb_func
ks37_input_scanner_hook:
    push {r0-r3,r12,lr}
    mrs r12,apsr
    sub sp,sp,#8
    str r12,[sp]
    movs r0,#2
    movs r2,#0x7e
    mov r3,r5
    bl ks37_note_input_entry
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add sp,sp,#8
    pop {r0-r3,r12,lr}
    ldr pc, .Lscanner_original
.size ks37_input_scanner_hook,.-ks37_input_scanner_hook
.p2align 2
.Lscanner_original: .word 0x0801b751

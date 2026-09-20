.syntax unified
.cpu cortex-m3
.thumb

/* Physical Mode is a native slot selector; while GEN owns playback, defer its
 * dispatch acknowledgement rather than leaving a request after a denied
 * pending-pointer write. Original debounce54/55 remains live. Once GEN leaves,
 * original5a20 observes55!=6d and dispatches the latest physical position once.
 * No private RAM and no native musical state writes on the suppressed route.
 * Both windows preserve incoming registers, SP, LR and APSR NZCVQ. */
.macro mode12_probe kind,receiver_r4=0
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
.if \receiver_r4
    mov r0,r4
.endif
    movs r1,#\kind
    bl ks37_generator_native_block
    cmp r0,#0
.endm
.macro mode12_restore
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add sp,#8
    pop {r0-r3,r12,lr}
.endm

/* All callers of actual native slot/arp-pattern handler17ed0. This is distinct
 * from existing16968 ARP/SEQ switch guard. */
.section .text.ks37_generator_slot_mode_guard12,"ax",%progbits
.global ks37_generator_slot_mode_guard12
.type ks37_generator_slot_mode_guard12,%function
.thumb_func
ks37_generator_slot_mode_guard12:
    mode12_probe 26
    beq 1f
    mode12_restore
    bx lr
1:
    mode12_restore
    push {r4,lr}
    sub sp,#8
    ldr pc,.Lslot_resume12
.size ks37_generator_slot_mode_guard12,.-ks37_generator_slot_mode_guard12
.p2align 2
.Lslot_resume12: .word 0x08017ed5

/* 5a44 is after native debounce and before reported6d changes or H+b9 event.
 * Skip to original5a3c epilogue with original four-word frame still allocated.
 * In particular no released stack slot is used as a later return address. */
.section .text.ks37_generator_mode_ack_guard12,"ax",%progbits
.global ks37_generator_mode_ack_guard12
.type ks37_generator_mode_ack_guard12,%function
.thumb_func
ks37_generator_mode_ack_guard12:
    mode12_probe 27,1
    beq 2f
    mode12_restore
    ldr pc,.Lmode_skip12
2:
    mode12_restore
    strb.w r0,[r4,#0x6d]
    ldr pc,.Lmode_resume12
.size ks37_generator_mode_ack_guard12,.-ks37_generator_mode_ack_guard12
.p2align 2
.Lmode_skip12: .word 0x08005a3d
.Lmode_resume12: .word 0x08005a49

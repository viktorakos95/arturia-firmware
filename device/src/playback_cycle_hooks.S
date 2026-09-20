.syntax unified
.cpu cortex-m3
.thumb

/* 08012eb6 original MOVS r3,#0 / STRB r3,[r4,#6]. Observe native C6
 * before it is cleared for the actual12ec0 consumer invocation. r4=C,
 * r9=selected step, r6=authentic native service phase. This marker alone is
 * never a musical cycle; paired PRE still validates all playback state. */
.section .text.ks37_playback_cycle_hook,"ax",%progbits
.global ks37_playback_cycle_hook
.type ks37_playback_cycle_hook,%function
.thumb_func
ks37_playback_cycle_hook:
    sub sp,#40
    stmia sp,{r0-r3,r12,lr}
    mrs r12,APSR
    str r12,[sp,#24]
    mrs r12,PRIMASK
    str r12,[sp,#32]
    cpsid i
    mov r0,r4
    mov r1,r9
    ldrb r2,[r4,#6]
    mov r3,r6
    bl ks37_playback_cycle_observe
    /* Replay original store and its exact MOVS flag effects under the same
     * mask. Save those native r3/APSR outputs into the still allocated frame. */
    ldr r12,[sp,#24]
    msr APSR_nzcvq,r12
    movs r3,#0
    strb r3,[r4,#6]
    str r3,[sp,#12]
    mrs r12,APSR
    str r12,[sp,#24]
    ldr r12,[sp,#32]
    msr PRIMASK,r12
    ldr r12,[sp,#24]
    msr APSR_nzcvq,r12
    ldmia sp,{r0-r3,r12,lr}
    add sp,#40
    ldr pc,.Lcycle_continue
.size ks37_playback_cycle_hook,.-ks37_playback_cycle_hook
.p2align 2
.Lcycle_continue: .word 0x08012ebb

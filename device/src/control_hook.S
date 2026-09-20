.syntax unified
.cpu cortex-m3
.thumb

/* Replace stock 08004930 STRB.W with B.W (not BL) to this entry.
 * Stock frame: r4=object, r2=old raw, r3=new raw; SP is 8-byte aligned.
 * Replay the displaced store before calling C. Preserve APSR and all caller
 * registers, including LR. C obeys AAPCS for r4-r11. Neither continuation
 * depends on our return value: they receive the original machine state.
 * Symbols for the two stock continuations are supplied by the linker.
 */
.section .text.ks37_control_hook,"ax",%progbits
.global ks37_control_hook
.type ks37_control_hook,%function
.thumb_func
ks37_control_hook:
    strb.w r3, [r4, #0x5a]
    push {r0-r3, r12, lr}
    mrs r12, apsr
    push {r12, lr}
    mov r0, r4
    ldrb.w r1, [r4, #0x58]
    bl ks37_control_input_entry
    cmp r0, #1
    beq .Lconsume
.Lpass:
    ldr r12, [sp]
    msr apsr_nzcvq, r12
    add sp, #8
    pop {r0-r3, r12, lr}
    b.w ks37_control_pass
.Lconsume:
    ldr r12, [sp]
    msr apsr_nzcvq, r12
    add sp, #8
    pop {r0-r3, r12, lr}
    b.w ks37_control_consume
.size ks37_control_hook, .-ks37_control_hook

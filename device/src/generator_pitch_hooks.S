.syntax unified
.cpu cortex-m3
.thumb

/* Replace only the consumer's BL08014034 to0801c384. At that call r0 is
 * signed sequence pitch, r1 selected, r2 H+4f. The consumer's88-byte frame
 * is8-aligned. Outside GEN tail-call the original helper with exact inputs,
 * scratch registers, flags and original LR. Owned GEN bypasses only this
 * transpose, using the same uint8 return convention as the native helper.
 * Native selected/H state is never written, saved or restored here. */
.section .text.ks37_generator_pitch_hook,"ax",%progbits
.global ks37_generator_pitch_hook
.type ks37_generator_pitch_hook,%function
.thumb_func
ks37_generator_pitch_hook:
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    bl ks37_generator_pitch_owned
    cbz r0,1f
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    ldr lr,[sp,#28]
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    add sp,#32
    uxtb r0,r0
    bx lr
1:
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    ldr lr,[sp,#28]
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    add sp,#32
    b.w ks37_generator_pitch_native
.size ks37_generator_pitch_hook,.-ks37_generator_pitch_hook

/* Linker pin: Thumb function pointer0801c385; code entry0801c384. */
.extern ks37_generator_pitch_native

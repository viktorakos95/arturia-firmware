.syntax unified
.cpu cortex-m3
.thumb

/* Main157b4 callsite: original129cc once, with original args. PRE/POST hooks
 * run outside the native service frame. Preserve native return state, even
 * though this particular caller immediately calls the tick getter. */
.section .text.ks37_generator_main_hook,"ax",%progbits
.global ks37_generator_main_hook
.type ks37_generator_main_hook,%function
.thumb_func
ks37_generator_main_hook:
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    bl ks37_generator_main_before
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    bl __gd_stock_service
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    bl ks37_generator_main_after
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    ldr lr,[sp,#60]
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    add sp,#64
    bx lr
.size ks37_generator_main_hook,.-ks37_generator_main_hook
.extern __gd_stock_service

/* Whole-function guards: no original effects occur until permission. The
 * replay-specific tail appears immediately after the macro. All entry args,
 * caller registers, original SP and NZCVQ are preserved on both routes. */
.macro guard name, kind
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
.thumb_func
\name:
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    movs r1,#\kind
    bl ks37_generator_native_block
    cmp r0,#0
    beq 1f
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add sp,#8
    pop {r0-r3,r12,lr}
    bx lr
1:
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add sp,#8
    pop {r0-r3,r12,lr}
.endm

guard ks37_generator_note_guard,0
    push.w {r4-r11,lr}
    ldr pc, .Lnote_resume
.size ks37_generator_note_guard,.-ks37_generator_note_guard
.p2align 2
.Lnote_resume: .word 0x0801b755

guard ks37_generator_ui_guard,1
    push {r4-r6,lr}
    sub sp,#72
    ldr pc, .Lui_resume
.size ks37_generator_ui_guard,.-ks37_generator_ui_guard
.p2align 2
.Lui_resume: .word 0x08017265

guard ks37_generator_mode_guard,2
    push {r3-r5,lr}
    mov r4,r0
    ldr pc, .Lmode_resume
.size ks37_generator_mode_guard,.-ks37_generator_mode_guard
.p2align 2
.Lmode_resume: .word 0x0801696d

guard ks37_generator_current_guard,3
    str r1,[r0]
    bx lr
.size ks37_generator_current_guard,.-ks37_generator_current_guard

guard ks37_generator_pending_guard,4
    str r1,[r0,#4]
    bx lr
.size ks37_generator_pending_guard,.-ks37_generator_pending_guard

guard ks37_generator_pending_apply_guard,5
    ldr r3,[r0,#4]
    str r3,[r0]
    bx lr
.size ks37_generator_pending_apply_guard,.-ks37_generator_pending_apply_guard

guard ks37_generator_cell_guard,6
    push {r4,r5}
    ldr r5,[r0]
    ldr pc, .Lcell_resume
.size ks37_generator_cell_guard,.-ks37_generator_cell_guard
.p2align 2
.Lcell_resume: .word 0x080130d1

guard ks37_generator_length_guard,7
    ldr r3,[r0]
    cmp r1,#64
    ldr pc, .Llength_resume
.size ks37_generator_length_guard,.-ks37_generator_length_guard
.p2align 2
.Llength_resume: .word 0x08013107

guard ks37_generator_gate_guard,8
    ldr r3,[r0]
    strb.w r1,[r3,#0x402]
    ldr pc, .Lgate_resume
.size ks37_generator_gate_guard,.-ks37_generator_gate_guard
.p2align 2
.Lgate_resume: .word 0x0801311f

guard ks37_generator_swing_guard,9
    ldr r3,[r0]
    strb.w r1,[r3,#0x401]
    ldr pc, .Lswing_resume
.size ks37_generator_swing_guard,.-ks37_generator_swing_guard
.p2align 2
.Lswing_resume: .word 0x08013135

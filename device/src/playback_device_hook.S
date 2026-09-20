.syntax unified
.cpu cortex-m3
.thumb

/* Both hooks are transparent around the actual native body. The first frame
 * keeps original args/LR/APSR, the second saves the native return registers
 * across POST. Each frame is32 bytes; stock and C callbacks see aligned SP.
 * Callback obeys AAPCS r4-r11 preservation, including live r7/r9/r10 at12ec0. */
.macro playback_wrapper name, pre, post, stock
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
.thumb_func
\name:
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    .if \pre == 0
    bl ks37_playback_allow
    cbnz r0,1f
    /* Denied transport: no PRE/native/POST; return original registers/flags. */
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    ldr lr,[sp,#28]
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    add sp,#32
    bx lr
1:
    ldr r0,[sp,#8]
    ldr r1,[sp,#12]
    .endif
    mov r2,r1
    mov r1,r0
    movs r0,#\pre
    bl ks37_playback_event
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    bl \stock
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    ldr r1,[sp,#40]
    ldr r2,[sp,#44]
    movs r0,#\post
    bl ks37_playback_event
    ldr r12,[sp]
    msr apsr_nzcvq,r12
    ldr lr,[sp,#60]
    add r12,sp,#8
    ldm r12,{r0-r3,r12}
    add sp,#64
    bx lr
.size \name,.-\name
.endm

playback_wrapper ks37_playback_transport_hook,0,1,ks37_playback_stock_transport
playback_wrapper ks37_playback_step_hook,2,3,ks37_playback_stock_step

.section .text.ks37_playback_stock_transport,"ax",%progbits
.global ks37_playback_stock_transport
.type ks37_playback_stock_transport,%function
.thumb_func
ks37_playback_stock_transport:
    /* displaced12334 f0 b5 85 b0; continuation instruction12338 */
    push {r4-r7,lr}
    sub sp,#20
    b.w ks37_playback_transport_continue
.size ks37_playback_stock_transport,.-ks37_playback_stock_transport

/* Linker pins (not .equ to keep relocation range correct):
 * ks37_playback_transport_continue = 0x08012339
 * ks37_playback_stock_step = 0x08013e8d
 * patch12334 B.W transport_hook; patch12ec0 BL step_hook.
 * No patch or final Runtime binding is supplied by this component. */
.extern ks37_playback_transport_continue
.extern ks37_playback_stock_step

.syntax unified
.cpu cortex-m3
.thumb
.text
.align 2

/* Every callback sees an8-aligned stack. SAVE/RESTORE retain exact NZCVQ and
 * r0-r3/r12/LR; r4-r11 use the ordinary AAPCS callee-preservation contract. */
.macro SAVE
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r3,APSR
    str r3,[sp]
.endm
.macro RESTORE
    ldr r3,[sp]
    msr APSR_nzcvq,r3
    add sp,#8
    pop {r0-r3,r12,lr}
.endm
.macro FN name
    .global \name
    .type \name,%function
    .thumb_func
\name:
.endm
.macro END name
    .size \name,.-\name
.endm

/* Constructor original frame8: displaced pop{r4,r5};bx lr. */
FN ks37_button_constructor_hook
    SAVE
    bl ks37_button_constructor_entry
    RESTORE
    pop {r4,r5}
    bx lr
END ks37_button_constructor_hook

/* Called instead of BL1a760 at1a7c2. At entry SP=P, original poll frame16.
 * Persistent layout after return to stock:
 * P-80 copied frame16, with return=cleanup
 * P-64 BdaPoll64, containing this poll's actual edge/native call tickets
 * P    untouched original stock frame16.
 * Temporary48 below P-80 disappear BEFORE stock poll resumes. No stock poll
 * instruction uses SP-relative locals other than its final pop. */
FN ks37_button_observer_hook
    sub sp,#128
    stmia sp,{r0-r3,r12,lr}
    str r1,[sp,#28]
    bl __bda_stock_debounce
    stmia sp,{r0-r3,r12}
    mrs r3,APSR
    str r3,[sp,#24]
    ldr r0,[sp,#128]
    str r0,[sp,#48]
    ldr r0,[sp,#132]
    str r0,[sp,#52]
    ldr r0,[sp,#136]
    str r0,[sp,#56]
    ldr r0,=ks37_button_poll_return
    str r0,[sp,#60]
    add r0,sp,#64
    mov r1,r4
    ldr r2,[sp,#28]
    bl ks37_button_poll_entry
    ldr r3,[sp,#24]
    msr APSR_nzcvq,r3
    ldmia sp,{r0-r3,r12,lr}
    add sp,#48
    ldr pc,.Lpoll_continue
END ks37_button_observer_hook
.align 2
.Lpoll_continue: .word 0x0801a7c7
.ltorg

/* Original pop has restored r3/r4/r5; SP now addresses the64-byte capsule.
 * Original caller return is at capsule+76. Restore exact original caller SP
 * while branching without spending a live general-purpose register. */
FN ks37_button_poll_return
    SAVE
    add r0,sp,#32
    bl ks37_button_poll_end_entry
    ldr r0,[sp,#108]
    str r0,[sp,#32]
    RESTORE
    ldr pc,[sp],#80
END ks37_button_poll_return

/* Find capsule from a real handler frame only after verifying BOTH callers.
 * base is32 for native hooks,120 for UI witness (SAVE32+UIframe88).
 * Only poll's three literal BL returns are accepted. A foreign native call
 * gets NULL and continues stock; it never obtains a physical path ticket. */
.macro CAPSULE base,frame,down
    ldr r3,[sp,#(\base+\frame-4)]
    .if \down
        ldr r2,=0x0801a813
        cmp r3,r2
        bne .Lforeign\@
    .else
        ldr r2,=0x0801a841
        cmp r3,r2
        beq .Lcaller\@
        ldr r2,=0x0801a853
        cmp r3,r2
        bne .Lforeign\@
    .endif
.Lcaller\@:
    add r0,sp,#(\base+\frame+16)
    ldr r3,[r0,#-4]
    ldr r2,=ks37_button_poll_return
    cmp r3,r2
    beq .Lcapsule\@
.Lforeign\@:
    movs r0,#0
.Lcapsule\@:
.endm

FN ks37_button_press_pre_hook
    SAVE
    CAPSULE 32,72,1
    mov r1,r4
    movs r2,#1
    bl ks37_button_native_pre_entry
    cmp r0,#0
    bne .Lpress_consume
    RESTORE
    ldr r3,.Lh
    ldrb r5,[r3,#0xb9]
    ldr pc,.Lpress_continue
.Lpress_consume:
    RESTORE
    ldr pc,.Lpress_exit
END ks37_button_press_pre_hook
.align 2
.Lh: .word 0x200051cc
.Lpress_continue: .word 0x08019fe9
.Lpress_exit: .word 0x0801a501
.ltorg

FN ks37_button_release_pre_hook
    SAVE
    CAPSULE 32,56,0
    mov r1,r4
    movs r2,#0
    bl ks37_button_native_pre_entry
    cmp r0,#0
    bne .Lrelease_consume
    RESTORE
    ldr r3,.Lh_release
    ldrb r3,[r3,#0xb9]
    ldr pc,.Lrelease_continue
.Lrelease_consume:
    RESTORE
    ldr pc,.Lrelease_exit
END ks37_button_release_pre_hook
.align 2
.Lh_release: .word 0x200051cc
.Lrelease_continue: .word 0x0801a55f
.Lrelease_exit: .word 0x0801a5e5
.ltorg

FN ks37_button_press_post_hook
    SAVE
    CAPSULE 32,72,1
    mov r1,r4
    movs r2,#1
    bl ks37_button_native_post_entry
    RESTORE
    add sp,#52
    pop {r4-r7,pc}
END ks37_button_press_post_hook
.ltorg

FN ks37_button_release_post_hook
    SAVE
    CAPSULE 32,56,0
    mov r1,r4
    movs r2,#0
    bl ks37_button_native_post_entry
    RESTORE
    add sp,#44
    pop {r4,r5,pc}
END ks37_button_release_post_hook
.ltorg

FN ks37_button_chord_down_hook
    movs r3,#1
    strb r3,[r0,#13]
    SAVE
    ldr r3,[sp,#116]
    ldr r2,=0x0801a501
    cmp r3,r2
    bne .Ldown_foreign
    CAPSULE 120,72,1
    b .Ldown_observe
.Ldown_foreign:
    movs r0,#0
.Ldown_observe:
    ldr r1,[sp,#8]
    movs r2,#1
    bl ks37_button_path_entry
    RESTORE
    ldr pc,.Ldown_continue
END ks37_button_chord_down_hook
.align 2
.Ldown_continue: .word 0x0801785b
.ltorg

FN ks37_button_chord_up_hook
    movs r3,#0
    strb r3,[r0,#13]
    SAVE
    ldr r3,[sp,#116]
    ldr r2,=0x0801a5e5
    cmp r3,r2
    bne .Lup_foreign
    CAPSULE 120,56,0
    b .Lup_observe
.Lup_foreign:
    movs r0,#0
.Lup_observe:
    ldr r1,[sp,#8]
    movs r2,#0
    bl ks37_button_path_entry
    RESTORE
    ldr pc,.Lup_continue
END ks37_button_chord_up_hook
.align 2
.Lup_continue: .word 0x08017d9d
.ltorg

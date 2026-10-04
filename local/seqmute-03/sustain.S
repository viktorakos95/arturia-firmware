/* KeyStep 37 (original) 1.1.6.579 — Hold button as internal note sustain.
 * Assembled as one section whose offset 0 is flash 0x08014000, so every branch
 * between stock labels, hook sites and the added code is resolved by the assembler.
 * Only the hook-site windows and the added block are copied into the image. */
        .syntax unified
        .cpu cortex-m3
        .thumb
        .text
        .equ BASE, 0x08014000
        .macro AT addr
        .org \addr - BASE
        .endm
        .macro STOCK name, addr
        AT \addr
        .thumb_func
\name:
        .endm

/* ---- stock entry points (labels only, no bytes emitted) and hook sites ---- */
        STOCK stock_hold_clear, 0x080149aa      /* (HO, mode): clears HO+6/HO+5 when mode bit0 */
        AT 0x08014a2c
site_pedal_off:      bl hold_off_pedal          /* was: bl stock_hold_clear */
        AT 0x08014a84
site_pedal_release:  bl hold_off_pedal
        AT 0x08014afa
site_button_off:     bl hold_off_button
        AT 0x08014b52
site_button_release: bl hold_off_button
        STOCK stock_get_setting, 0x080160a8     /* (H, index) -> channel */
        STOCK stock_note_out, 0x0801b384        /* (out, msg, source mask, route) */
        AT 0x0801b75e
site_note_entry:     bl hook_note_entry         /* was: adds r5,r0,#4 ; mov r0,r5 */
        AT 0x0801bfa8
site_note_off:       b.w hook_note_off          /* was: tst.w r3,#2 (beq at 0x0801bfac now dead) */
        STOCK stock_off_send, 0x0801bfae        /* stock: tst.w r3,#0x80 ... send Note Off, clear flag */
        STOCK stock_off_clear, 0x0801bfd6       /* stock: clear flag only */
        STOCK stock_off_tail, 0x0801bfde        /* stock: CV/selection tail */

/* ---- added code, in the 0xFF padding after .data inside the last payload record ---- */
        AT 0x0801f180
        .equ NOTE_OBJ, 0x20001eb8
        .equ FLAGS, 0x496                       /* per-pitch byte array in the note object */
        .equ F_SENT, 0x02                       /* stock: direct Note On was sent */
        .equ F_PENDING, 0x04                    /* new: Note Off withheld by Hold */
        .equ F_ALTCH, 0x80                      /* stock: sent on the alternate keyboard channel */

        .thumb_func
hook_note_off:          /* r3 flag, r6 note object, r8 pitch, sb source (0 = physical keys) */
        tst.w   r3, #F_SENT
        beq     1f
        cmp.w   sb, #0
        bne     2f
        ldr     r2, =0x2000115c
        ldr     r2, [r2]
        ldrb    r2, [r2, #5]                    /* Hold state */
        cbz     r2, 2f
        orr     r3, r3, #F_PENDING
        add.w   r2, r6, r8
        strb.w  r3, [r2, #FLAGS]
        b.w     stock_off_tail
1:      b.w     stock_off_clear
2:      b.w     stock_off_send

        .thumb_func
hook_note_entry:        /* r0 = r6 = note object, caller [sp,#4] = message; must set r5 = r0+4, r0 = r5 */
        push    {r4, lr}
        ldr     r1, [sp, #12]
        and     r2, r1, #0xf0
        cmp     r2, #0x90
        bne     3f
        ubfx    r2, r1, #16, #7
        cbz     r2, 3f                          /* velocity 0 is a release */
        ubfx    r4, r1, #8, #7
        adds    r2, r6, r4
        ldrb.w  r3, [r2, #FLAGS]
        tst.w   r3, #F_PENDING
        beq     3f
        mov     r0, r6
        mov     r1, r4
        bl      send_off                        /* end the sustained note so the new press retriggers */
3:      adds    r5, r6, #4
        mov     r0, r5
        pop     {r4, pc}

        .thumb_func
send_off:               /* r0 note object, r1 pitch, r3 flag; sends Note Off, clears the flag */
        push    {r4, r5, r6, lr}
        mov     r6, r0
        mov     r4, r1
        mov     r5, r3
        tst.w   r5, #F_ALTCH
        beq     4f
        movs    r1, #4
        ldr     r0, =0x200051cc
        bl      stock_get_setting
        b       5f
4:      ldr     r0, =0x200010d4
        ldrb    r0, [r0]
5:      and     r0, r0, #0x0f
        orr     r0, r0, #0x80
        orr.w   r1, r0, r4, lsl #8
        orr     r1, r1, #0x400000               /* release velocity 0x40, as stock */
        ldr     r0, [r6]
        movs    r2, #1                          /* source: physical keys */
        movs    r3, #0
        tst.w   r5, #F_ALTCH
        it      ne
        movne   r3, #2
        bl      stock_note_out
        adds    r2, r6, r4
        movs    r3, #0
        strb.w  r3, [r2, #FLAGS]
        pop     {r4, r5, r6, pc}

        .thumb_func
release_all:
        push    {r4, r5, r6, lr}
        ldr     r6, =NOTE_OBJ
        movs    r4, #0
6:      adds    r2, r6, r4
        ldrb.w  r3, [r2, #FLAGS]
        tst.w   r3, #F_PENDING
        beq     7f
        mov     r0, r6
        mov     r1, r4
        bl      send_off
7:      adds    r4, #1
        cmp     r4, #128
        bne     6b
        pop     {r4, r5, r6, pc}

        .thumb_func
hold_off_button:        /* r0 Hold object, r1 mode; caller clears HO+5 right after */
        movs    r3, #0
        strb    r3, [r0, #5]
        .thumb_func
hold_off_pedal:
        push    {r4, lr}
        mov     r4, r0
        bl      stock_hold_clear
        ldrb    r3, [r4, #5]
        cbnz    r3, 8f
        bl      release_all
8:      pop     {r4, pc}
        .ltorg
        .balign 4
added_end:

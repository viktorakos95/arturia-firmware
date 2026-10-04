/* KeyStep 37 (original) 1.1.6.579 — paraphonic keyboard split, Shift+Hold on/off (on top of seqmute-03).
 * Assembled as one section whose offset 0 is flash 0x08004000, so every branch
 * between stock labels, hook sites and the added code is resolved by the assembler.
 * Only the hook-site windows and the added block are copied into the image. */
        .syntax unified
        .cpu cortex-m3
        .thumb
        .text
        .equ BASE, 0x08004000
        .macro AT addr
        .org \addr - BASE
        .endm
        .macro STOCK name, addr
        AT \addr
        .thumb_func
\name:
        .endm

/* ---- stock entry points (labels only, no bytes emitted) and hook sites ---- */
        AT 0x0800496c                           /* chord-page knob jump table (tbh, base = this address) */
site_knob_table:
        .short (knob_split - site_knob_table) / 2
        .short (knob_mode - site_knob_table) / 2
        .short (stock_knob_exit - site_knob_table) / 2      /* knobs 3 and 4: no action on the split page */
        .short (stock_knob_exit - site_knob_table) / 2
        STOCK stock_knob_exit, 0x08004986       /* epilogue of the knob control function */
        STOCK stock_hold_clear, 0x080149aa      /* (HO, mode): clears HO+6/HO+5 when mode bit0 */
        AT 0x08014a2c
site_pedal_off:      bl hold_off_pedal          /* was: bl stock_hold_clear */
        AT 0x08014a84
site_pedal_release:  bl hold_off_pedal
        AT 0x08014afa
site_button_off:     bl hold_off_button
        AT 0x08014b52
site_button_release: bl hold_off_button
        STOCK stock_midi_send, 0x08014b7c       /* (unused, msg): send to USB and DIN */
        STOCK stock_get_setting, 0x080160a8     /* (H, index) -> channel */
        AT 0x08017856
site_chord_press:    movs r3, #0                /* was movs r3,#1: Chord held no longer arms chord capture */
        AT 0x0801786c
site_shift_hold:     .short (split_toggle - site_shift_hold) / 2   /* Shift+Hold press: split on/off instead of chord mode */
        STOCK router_exit, 0x08017b0e           /* epilogue of the button router */
        STOCK stock_note_out, 0x0801b384        /* (out, msg, source mask, route) */
        AT 0x0801b75e
site_note_entry:     bl hook_note_entry         /* was: adds r5,r0,#4 ; mov r0,r5 */
        AT 0x0801bfa8
site_note_off:       b.w hook_note_off          /* was: tst.w r3,#2 (beq at 0x0801bfac now dead) */
        STOCK stock_off_send, 0x0801bfae        /* stock: tst.w r3,#0x80 ... send Note Off, clear flag */
        STOCK stock_off_clear, 0x0801bfd6       /* stock: clear flag only */
        STOCK stock_off_tail, 0x0801bfde        /* stock: CV/selection tail */
        STOCK stock_note_exit, 0x0801c1aa       /* epilogue of the note function */
        STOCK stock_disp_value, 0x0801c9ac      /* (disp) -> pointer to the value being shown */
        STOCK stock_disp_show, 0x0801cd5a       /* (disp, formatter, 0, value) */
        STOCK stock_disp_char, 0x0801d580       /* (driver, position, ascii) */
        STOCK stock_disp_str, 0x0801d660        /* (driver, string) */

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
        beq     9f
        mov     r0, r6
        mov     r1, r4
        bl      send_off                        /* end the sustained note so the new press retriggers */
9:      bl      split_check                     /* r4 = pitch; r0 != 0: this key sends no note */
        cbnz    r0, 10f
3:      adds    r5, r6, #4
        mov     r0, r5
        pop     {r4, pc}
10:     pop.w   {r4, lr}
        b.w     stock_note_exit

/* Paraphonic split, physical keys only, on while split flag is set (Shift+Hold toggles it).
 * Chord object +9: bit7 = split on, bits0-1 = gate side (0 "1", 1 "2", 2 "ALL"; stock constructor leaves 3 = ALL).
 * Chord object +0x48 = split point (0 -> 60).
 * "1": keys >= split send CC 17 only.          "2": keys < split send CC 16 only.
 * "ALL": keys >= split send CC 17, and also play as a note when no lower-side note is held. */
        .thumb_func
split_check:
        push    {r5, lr}
        cmp.w   sb, #0
        bne     20f
        ldr     r3, =0x20001e04
        ldrb    r5, [r3, #9]
        tst.w   r5, #0x80
        beq     20f                             /* split off */
        and     r5, r5, #3
        cmp     r5, #2
        it      hi
        movhi   r5, #2
        ldrb.w  r2, [r3, #0x48]
        cbnz    r2, 11f
        movs    r2, #60
11:     cmp     r4, r2
        bhs     13f
        cmp     r5, #1                          /* lower side */
        bne     20f
        movs    r1, #16
        bl      send_cc
        b       21f
13:     cmp     r5, #1                          /* upper side */
        beq     20f
        movs    r1, #17
        mov     r3, r2
        push    {r3, r5}
        bl      send_cc
        pop     {r2, r5}
        cmp     r5, #2
        bne     21f
        movs    r0, #0                          /* ALL: is any lower-side note sounding? */
14:     cmp     r0, r2
        bhs     20f
        adds    r1, r6, r0
        ldrb.w  r1, [r1, #FLAGS]
        lsls    r1, r1, #30
        bmi     21f
        adds    r0, #1
        b       14b
20:     movs    r0, #0
        pop     {r5, pc}
21:     movs    r0, #1
        pop     {r5, pc}

        .thumb_func
send_cc:                /* r1 = controller number, r4 = value; keyboard channel, USB and DIN */
        ldr     r0, =0x200010d4
        ldrb    r0, [r0]
        and     r0, r0, #0x0f
        orr     r0, r0, #0xb0
        orr.w   r0, r0, r1, lsl #8
        orr.w   r1, r0, r4, lsl #16
        b.w     stock_midi_send

/* chord-page knobs; run inside the stock knob function (r4 = knob object, +0x59 raw 0..255) */
        .thumb_func
knob_split:
        ldrb.w  r3, [r4, #0x59]
        movs    r2, #85
        muls    r3, r2
        lsrs    r3, r3, #8
        adds    r3, #24                         /* 24..108 */
        ldr     r2, =0x20001e04
        strb.w  r3, [r2, #0x48]
        adr     r1, fmt_note
        b       15f
        .thumb_func
knob_mode:
        ldrb.w  r3, [r4, #0x59]
        add.w   r3, r3, r3, lsl #1
        lsrs    r3, r3, #8                      /* 0..2 */
        ldr     r2, =0x20001e04
        ldrb    r1, [r2, #9]
        and     r1, r1, #0x80                   /* keep the split on/off bit */
        orrs    r1, r3
        strb    r1, [r2, #9]
        adr     r1, fmt_mode
15:     adds    r1, #1                          /* Thumb function pointer */
        movs    r2, #0
        ldr     r0, =0x20001180
        bl      stock_disp_show
        b.w     stock_knob_exit

/* Shift+Hold: split on/off, shown as "ON " / "OFF" (display values 4 / 3 of fmt_mode); runs inside the button router */
        .thumb_func
split_toggle:
        ldr     r2, =0x20001e04
        ldrb    r3, [r2, #9]
        eor     r3, r3, #0x80
        strb    r3, [r2, #9]
        lsrs    r3, r3, #7
        adds    r3, #3
        adr     r1, fmt_mode
        adds    r1, #1
        movs    r2, #0
        ldr     r0, =0x20001180
        bl      stock_disp_show
        b.w     router_exit

        .balign 4
        .thumb_func
fmt_note:               /* display formatter: note name, middle C = C3 */
        push    {r4, r5, r6, lr}
        mov     r4, r0
        bl      stock_disp_value
        ldrb    r5, [r0]
        movs    r1, #12
        udiv    r6, r5, r1
        mls     r5, r1, r6, r5
        adr     r3, note_names
        add.w   r5, r3, r5, lsl #1
        ldr     r0, [r4, #0x58]
        movs    r1, #0
        ldrb    r2, [r5]
        bl      stock_disp_char
        ldr     r0, [r4, #0x58]
        movs    r1, #1
        ldrb    r2, [r5, #1]
        bl      stock_disp_char
        ldr     r0, [r4, #0x58]
        movs    r1, #2
        add.w   r2, r6, #46                     /* '0' - 2 */
        bl      stock_disp_char
        pop     {r4, r5, r6, pc}
        .balign 4
        .thumb_func
fmt_mode:               /* display formatter: gate mode */
        push    {r4, lr}
        mov     r4, r0
        bl      stock_disp_value
        ldrb    r1, [r0]
        adr     r3, mode_names
        add.w   r1, r3, r1, lsl #2
        ldr     r0, [r4, #0x58]
        pop.w   {r4, lr}
        b.w     stock_disp_str
        .balign 4
note_names: .ascii "C DbD EbE F GbG AbA BbB "
mode_names: .asciz "1  ", "2  ", "ALL", "OFF", "ON "

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

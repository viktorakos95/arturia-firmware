/* KeyStep 37 (original) 1.1.6.579 — round-robin oscillator allocation (2 oscillators per track), Shift+Hold on/off (on top of seqmute-03).
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

        /* Chord object 0x20001e04 (chord mode is unreachable in this firmware, its bytes are free):
         *   +9    bit7 round robin on; bits0-1 scale index (3 = 0); bits2-4 base offset index
         *   +0x49 oscillator 1 key (pitch|0x80, 0 = free)   +0x4a oscillator 2 key
         *   +0x4b bits0-5 keys held, bit6 oscillator to take next, bit7 gate held open by Hold
         *   +0xb1 gate note (pitch|0x80, 0 = gate closed) */
        .equ CHORD, 0x20001e04
        .equ SLOT, 0x49
        .equ STATE, 0x4b
        .equ GATEP, 0xb1
        .equ NOTE_OBJ, 0x20001eb8
        .equ FLAGS, 0x496                       /* per-pitch byte array in the note object */
        .equ F_SENT, 0x02                       /* stock: direct Note On was sent */
        .equ F_PENDING, 0x04                    /* new: Note Off withheld by Hold */
        .equ F_ALTCH, 0x80                      /* stock: sent on the alternate keyboard channel */

/* ---- stock entry points (labels only, no bytes emitted) and hook sites ---- */
        AT 0x0800496c                           /* chord-page knob jump table (tbh, base = this address) */
site_knob_table:
        .short (knob_scale - site_knob_table) / 2
        .short (knob_offset - site_knob_table) / 2
        .short (stock_knob_exit - site_knob_table) / 2      /* knobs 3 and 4: no action */
        .short (stock_knob_exit - site_knob_table) / 2
        STOCK stock_knob_exit, 0x08004986       /* epilogue of the knob control function */
/* ---- round-robin code: the stock chord-page knob handlers (0x0800527c..0x080059ec, 1904 bytes) are unreachable since the
 * knob table above no longer dispatches to them; nothing else branches into them (checked over the whole disassembly). ---- */
        AT 0x0800527c
rr_a_start:
/* Round robin: every physical key takes one oscillator (the other is muted while a key is alone), pitch by 14-bit CC
 * (OSC1 CC 16/48, OSC2 CC 17/49), level CC 69 / 78 (127 used, 0 muted). One gate note for the whole chord:
 * opened by the first key, closed when the last key is up (or when Hold is released). */
        .thumb_func
rr_event:               /* r1 = message; returns r0 = 1 when handled */
        push    {r4, r5, r6, r7, lr}
        ldr     r5, =CHORD
        and     r2, r1, #0xf0
        ubfx    r4, r1, #8, #7
        ubfx    r7, r1, #16, #7
        cmp     r2, #0x80
        beq     20f
        cmp     r2, #0x90
        bne     30f
        cmp     r7, #0
        beq.w   20f
        ldrb    r2, [r5, #STATE]                /* ---- key down ---- */
        tst.w   r2, #0x3f
        bne     5f
        orr     r7, r7, #0x100                  /* first key: the gate opens */
        tst.w   r2, #0x80
        beq     4f
        bl      gate_off                        /* Hold still holds the old gate: close it, retrigger */
4:      movs    r2, #0
        strb    r2, [r5, #SLOT]
        strb    r2, [r5, #SLOT + 1]
5:      ldrb    r0, [r5, #SLOT]
        movs    r6, #0
        cbz     r0, 7f
        ldrb    r0, [r5, #SLOT + 1]
        movs    r6, #1
        cbz     r0, 7f
        ldrb    r2, [r5, #STATE]
        ubfx    r6, r2, #6, #1                  /* both busy: take the oldest */
7:      orr     r1, r4, #0x80
        add.w   r0, r5, #SLOT
        strb    r1, [r0, r6]
        ldrb    r2, [r5, #STATE]
        eor     r1, r6, #1
        bfi     r2, r1, #6, #1                  /* the other one is now the oldest */
        adds    r2, #1
        strb    r2, [r5, #STATE]
        mov     r0, r6
        mov     r1, r4
        bl      osc_pitch
        mov     r0, r6
        movs    r1, #127
        bl      osc_level
        tst.w   r7, #0x100
        beq     40f
        eor     r0, r6, #1
        movs    r1, #0
        bl      osc_level                       /* first key alone: the other oscillator is muted */
        orr     r1, r4, #0x80
        strb    r1, [r5, #GATEP]
        movs    r0, #0x90
        mov     r1, r4
        and     r2, r7, #0x7f
        bl      midi3
        b       40f
20:     ldrb    r2, [r5, #STATE]                /* ---- key up ---- */
        tst.w   r2, #0x3f
        beq     40f                             /* not a key we took */
        subs    r2, #1
        strb    r2, [r5, #STATE]
        orr     r1, r4, #0x80
        movs    r6, #0
8:      add.w   r0, r5, #SLOT
        ldrb    r0, [r0, r6]
        cmp     r0, r1
        beq     9f
        adds    r6, #1
        cmp     r6, #2
        bne     8b
        b       10f
9:      add.w   r0, r5, #SLOT
        movs    r1, #0
        strb    r1, [r0, r6]
        ldrb    r2, [r5, #STATE]
        tst.w   r2, #0x3f
        beq     10f
        mov     r0, r6
        bl      osc_level                       /* other keys keep the gate open: mute the freed oscillator (r1 = 0) */
10:     ldrb    r2, [r5, #STATE]
        tst.w   r2, #0x3f
        bne     40f
        ldr     r0, =0x2000115c
        ldr     r0, [r0]
        ldrb    r0, [r0, #5]                    /* Hold state */
        cbz     r0, 11f
        orr     r2, r2, #0x80
        strb    r2, [r5, #STATE]                /* Hold: keep the gate until Hold is released */
        b       40f
11:     bl      gate_off
40:     movs    r0, #1
        pop     {r4, r5, r6, r7, pc}
30:     movs    r0, #0
        pop     {r4, r5, r6, r7, pc}

        .thumb_func
gate_off:               /* r5 = chord object: close the gate note if open, clear the Hold flag */
        push    {r3, lr}
        ldrb    r1, [r5, #GATEP]
        cbz     r1, 1f
        and     r1, r1, #0x7f
        movs    r0, #0x80
        movs    r2, #0x40
        bl      midi3
        movs    r1, #0
        strb    r1, [r5, #GATEP]
1:      ldrb    r1, [r5, #STATE]
        and     r1, r1, #0x7f
        strb    r1, [r5, #STATE]
        pop     {r3, pc}

        .thumb_func
midi3:                  /* r0 status (0x80/0x90/0xb0), r1 data 1, r2 data 2: keyboard channel, USB and DIN */
        ldr     r3, =0x200010d4
        ldrb    r3, [r3]
        and     r3, r3, #0x0f
        orrs    r0, r3
        orr.w   r0, r0, r1, lsl #8
        orr.w   r1, r0, r2, lsl #16
        b.w     stock_midi_send

        .thumb_func
osc_level:              /* r0 oscillator (0/1), r1 level */
        mov     r2, r1
        cmp     r0, #0
        ite     eq
        moveq   r1, #69
        movne   r1, #78
        movs    r0, #0xb0
        b       midi3

        .thumb_func
osc_pitch:              /* r0 oscillator (0/1), r1 note number: 14-bit pitch = 8192 + (note - 64 + offset) * scale */
        push    {r4, r6, lr}
        mov     r4, r0
        ldrb    r2, [r5, #9]
        ubfx    r3, r2, #2, #3
        adr     r6, tabs
        ldrsb.w r3, [r6, r3]
        subs    r1, #64
        adds    r1, r1, r3
        and     r2, r2, #3
        cmp     r2, #3
        it      eq
        moveq   r2, #0
        add.w   r2, r6, r2, lsl #1
        ldrh    r2, [r2, #8]
        muls    r1, r2
        add.w   r1, r1, #8192
        cmp     r1, #0
        it      lt
        movlt   r1, #0
        movw    r2, #16383
        cmp     r1, r2
        it      gt
        movgt   r1, r2
        mov     r6, r1
        movs    r0, #0xb0
        add.w   r1, r4, #16
        lsrs    r2, r6, #7
        bl      midi3
        movs    r0, #0xb0
        add.w   r1, r4, #48
        and     r2, r6, #0x7f
        bl      midi3
        pop     {r4, r6, pc}
        .balign 4
tabs:   .byte 0, -1, 1, -12, 12, 0, 0, 0        /* base offset by index, semitones */
        .short 128, 171, 341                    /* 14-bit units per semitone by index */

/* chord-page knobs; run inside the stock knob function (r4 = knob object, +0x59 raw 0..255) */
        .thumb_func
knob_scale:
        ldrb.w  r3, [r4, #0x59]
        add.w   r3, r3, r3, lsl #1
        lsrs    r3, r3, #8                      /* 0..2 */
        ldr     r2, =CHORD
        ldrb    r1, [r2, #9]
        bic     r1, r1, #3
        orrs    r1, r3
        strb    r1, [r2, #9]
        b       15f
        .thumb_func
knob_offset:
        ldrb.w  r3, [r4, #0x59]
        add.w   r3, r3, r3, lsl #2
        lsrs    r3, r3, #8                      /* 0..4 */
        ldr     r2, =CHORD
        ldrb    r1, [r2, #9]
        bic     r1, r1, #0x1c
        orr.w   r1, r1, r3, lsl #2
        strb    r1, [r2, #9]
        adds    r3, #5                          /* names 5..9 */
15:     ldr     r1, =fmt_mode                   /* Thumb function pointer */
        movs    r2, #0
        ldr     r0, =0x20001180
        bl      stock_disp_show
        b.w     stock_knob_exit

        .ltorg
        .balign 4
rr_a_end:
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


/* ---- added code, in the 0xFF padding after .data inside the last payload record (0x0801f16c..0x0801f3ff) ---- */
        AT 0x0801f180
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
        cmp.w   sb, #0
        bne     12f
        ldr     r2, =CHORD
        ldrb    r2, [r2, #9]
        tst.w   r2, #0x80
        beq     12f
        ldr     r1, [sp, #12]
        bl      rr_event
        cbz     r0, 12f
        pop.w   {r4, lr}
        b.w     stock_note_exit
12:     ldr     r1, [sp, #12]
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

/* Shift+Hold: round robin on/off, shown as "ON " / "OFF"; runs inside the button router */
        .thumb_func
split_toggle:
        push    {r4, r5, r6, lr}
        ldr     r5, =CHORD
        ldrb    r4, [r5, #9]
        eor     r4, r4, #0x80
        strb    r4, [r5, #9]
        bl      gate_off
        movs    r0, #0
        strb    r0, [r5, #STATE]
        strb    r0, [r5, #SLOT]
        strb    r0, [r5, #SLOT + 1]
        tst.w   r4, #0x80
        bne     16f
        movs    r6, #0                          /* off: both oscillators back to tune 0, level 127 */
13:     movs    r0, #0xb0
        adds    r1, r6, #16
        movs    r2, #64
        bl      midi3
        movs    r0, #0xb0
        adds    r1, r6, #48
        movs    r2, #0
        bl      midi3
        mov     r0, r6
        movs    r1, #127
        bl      osc_level
        adds    r6, #1
        cmp     r6, #2
        bne     13b
16:     lsrs    r3, r4, #7
        adds    r3, #3
        adr     r1, fmt_mode
        adds    r1, #1
        movs    r2, #0
        ldr     r0, =0x20001180
        bl      stock_disp_show
        pop     {r4, r5, r6, lr}
        b.w     router_exit

        .balign 4
        .thumb_func
fmt_mode:               /* display formatter: one of mode_names */
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
mode_names: .asciz "128", "171", "341", "OFF", "ON ", "0  ", "-1 ", "+1 ", "-12", "+12"

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

        .thumb_func
release_all:
        push    {r4, r5, r6, lr}
        ldr     r5, =CHORD
        ldrb    r3, [r5, #STATE]
        tst.w   r3, #0x80
        beq     14f
        bl      gate_off                        /* round robin: Hold released, close the gate */
14:     ldr     r6, =NOTE_OBJ
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

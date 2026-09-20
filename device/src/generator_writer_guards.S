.syntax unified
.cpu cortex-m3
.thumb
/* Whole-function entry guards for bounded native sequence/config writers.
 * Strong callback decides ownership. No C call after an original prologue.
 * Incoming 8-byte-aligned AAPCS stack; preserve all incoming values and NZCVQ.
 * Only record_note/rest return a semantic false result on suppression.
 * Root installs exact B.W windows from inspect_generator_writers.py.
 */
.macro writer_guard name,kind,zero
.section .text.ks37_generator_\name\()_guard,"ax",%progbits
.global ks37_generator_\name\()_guard
.type ks37_generator_\name\()_guard,%function
.thumb_func
ks37_generator_\name\()_guard:
    push {r0-r3,r12,lr}
    sub sp,#8
    mrs r12,apsr
    str r12,[sp]
    movs r1,#\kind
    bl ks37_generator_native_block
    cmp r0,#0
    beq 1f
.if \zero
    movs r0,#0
    str r0,[sp,#8]
.endif
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

writer_guard slot_dirty,10,0
    movs r3,#0
    ldr pc,.Lslot_dirty_resume
.size ks37_generator_slot_dirty_guard,.-ks37_generator_slot_dirty_guard
.p2align 2
.Lslot_dirty_resume: .word 0x0800de39

writer_guard builder,11,0
    push.w {r4-r9,lr}
    ldr pc,.Lbuilder_resume
.size ks37_generator_builder_guard,.-ks37_generator_builder_guard
.p2align 2
.Lbuilder_resume: .word 0x08011a21

writer_guard record_note,12,1
    push {r4-r6}
    sub sp,#12
    ldr pc,.Lrecord_note_resume
.size ks37_generator_record_note_guard,.-ks37_generator_record_note_guard
.p2align 2
.Lrecord_note_resume: .word 0x0801316f

writer_guard record_rest,13,1
    cmp r1,#0x82
    bne 7f
    ldr pc,.Lrecord_rest_taken
7:
    ldr pc,.Lrecord_rest_resume
.size ks37_generator_record_rest_guard,.-ks37_generator_record_rest_guard
.p2align 2
.Lrecord_rest_resume: .word 0x0801326b
.Lrecord_rest_taken: .word 0x0801326f

writer_guard merge_cell,14,0
    push.w {r4-r11,lr}
    ldr pc,.Lmerge_cell_resume
.size ks37_generator_merge_cell_guard,.-ks37_generator_merge_cell_guard
.p2align 2
.Lmerge_cell_resume: .word 0x080132d3

writer_guard reorder_cell,15,0
    push {r4-r7}
    movs r3,#0
    ldr pc,.Lreorder_cell_resume
.size ks37_generator_reorder_cell_guard,.-ks37_generator_reorder_cell_guard
.p2align 2
.Lreorder_cell_resume: .word 0x0801342f

writer_guard record_build,16,0
    push.w {r4-r9,lr}
    ldr pc,.Lrecord_build_resume
.size ks37_generator_record_build_guard,.-ks37_generator_record_build_guard
.p2align 2
.Lrecord_build_resume: .word 0x08013493

writer_guard shorten,17,0
    ldr r2,[r0]
    ldrb.w r3,[r2,#0x400]
    ldr pc,.Lshorten_resume
.size ks37_generator_shorten_guard,.-ks37_generator_shorten_guard
.p2align 2
.Lshorten_resume: .word 0x0801378f

writer_guard clear,18,0
    ldr r3,[r0]
    movs r2,#1
    ldr pc,.Lclear_resume
.size ks37_generator_clear_guard,.-ks37_generator_clear_guard
.p2align 2
.Lclear_resume: .word 0x080137c9

writer_guard record_length,19,0
    push {r4,r5}
    ldrb.w r3,[r0,#0x2e]
    ldr pc,.Lrecord_length_resume
.size ks37_generator_record_length_guard,.-ks37_generator_record_length_guard
.p2align 2
.Lrecord_length_resume: .word 0x080137f7

writer_guard pattern,20,0
    push {r4-r7}
    cmp r1,#8
    ldr pc,.Lpattern_resume
.size ks37_generator_pattern_guard,.-ks37_generator_pattern_guard
.p2align 2
.Lpattern_resume: .word 0x08013895

writer_guard tie,21,0
    push {r3-r7,lr}
    mov r4,r0
    ldr pc,.Ltie_resume
.size ks37_generator_tie_guard,.-ks37_generator_tie_guard
.p2align 2
.Ltie_resume: .word 0x08013949

writer_guard flush_record,22,0
    push {r3-r7,lr}
    ldrsb.w r3,[r0,#0x45]
    ldr pc,.Lflush_record_resume
.size ks37_generator_flush_record_guard,.-ks37_generator_flush_record_guard
.p2align 2
.Lflush_record_resume: .word 0x08013b8b

writer_guard clear_cells,23,0
    movs r3,#0
    cmp r3,#63
    ldr pc,.Lclear_cells_resume
.size ks37_generator_clear_cells_guard,.-ks37_generator_clear_cells_guard
.p2align 2
.Lclear_cells_resume: .word 0x08013c5d

writer_guard config_bytes,24,0
    push {r4,r5}
    ldrb r3,[r1,#10]
    ldr pc,.Lconfig_bytes_resume
.size ks37_generator_config_bytes_guard,.-ks37_generator_config_bytes_guard
.p2align 2
.Lconfig_bytes_resume: .word 0x0801441d

writer_guard config_words,25,0
    push {r4,r5}
    ldrb r3,[r1,#10]
    ldr pc,.Lconfig_words_resume
.size ks37_generator_config_words_guard,.-ks37_generator_config_words_guard
.p2align 2
.Lconfig_words_resume: .word 0x08014539

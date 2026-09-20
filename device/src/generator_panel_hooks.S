.syntax unified
.cpu cortex-m3
.thumb
.macro SAVE
 sub sp,#32
 stmia sp,{r0-r3,r12,lr}
 mrs r12,APSR
 str r12,[sp,#24]
.endm
.macro RESTORE
 ldr r12,[sp,#24]
 msr APSR_nzcvq,r12
 ldmia sp,{r0-r3,r12,lr}
 add sp,#32
.endm
.section .text.ks37_panel_pixel_hook,"ax",%progbits
.global ks37_panel_pixel_hook
.type ks37_panel_pixel_hook,%function
.thumb_func
ks37_panel_pixel_hook:
 /* Serialize stock shadow and the entire bounded24-halfword native write.
  * DMA may still read the existing live source; this is CPU exclusion only. */
 sub sp,#8
 str lr,[sp,#4]
 SAVE
 mrs r12,PRIMASK
 cmp r1,#40
 it ls
 orrls r12,r12,#256
 str r12,[sp,#32]
 cpsid i
 bl ks37_panel_pixel
 str r0,[sp,#8]
 RESTORE
 bl ks37_panel_writer_original
.Lwriter_return:
 SAVE
 ldr r0,[sp,#20]
 adr r1,.Lwriter_return
 orr r1,r1,#1
 cmp r0,r1
 bne 1f
 ldr r0,[sp,#36]
 str r0,[sp,#20]
1:
 ldr r0,[sp,#32]
 /* Accepted stock calls leave r1=their local color byte at SP+6. Translate
  * that otherwise dangling scratch-register pointer by our extra8bytes so
  * the exact stock post-call register value is preserved too. Invalid-index
  * early returns retain their original r1 and do not take this adjustment. */
 tst r0,#256
 beq 2f
 ldr r1,[sp,#4]
 add r1,r1,#8
 str r1,[sp,#4]
2:
 and r0,r0,#1
 msr PRIMASK,r0
 RESTORE
 /* Keep the caller return allocated until POP loads PC. An IRQ after freeing
  * all eight bytes would overwrite [sp,#-4] with its exception frame. */
 add sp,#4
 pop {pc}
.size ks37_panel_pixel_hook,.-ks37_panel_pixel_hook
.global ks37_panel_writer_original
.type ks37_panel_writer_original,%function
.thumb_func
ks37_panel_writer_original:
 push {r4,r5,lr}
 sub sp,#12
 ldr pc,=__gp_writer_continue
.size ks37_panel_writer_original,.-ks37_panel_writer_original

.section .text.ks37_panel_display_hook,"ax",%progbits
.global ks37_panel_display_hook
.type ks37_panel_display_hook,%function
.thumb_func
ks37_panel_display_hook:
 /* Replace add.w r1,r4,r8; mov r0,r7; bl stock_SPI_send as one10byte window.
  * Native frame has8bytes local, byte7 holds the address command. Overlay
  * source is an immutable FLASH byte, valid even for retained handle TX ptr.
  * No store touches L, its changed mask, or its refresh cursor. */
 SAVE
 mov r0,r4
 mov r1,r8
 add.w r2,r4,r8
 bl ks37_panel_display_source
 str r0,[sp,#4]
 RESTORE
 mov r0,r7
 bl __gp_spi_send
.Lspi_return:
 /* LR is caller-saved. Preserve a value left by a nonleaf stock callee; a
  * leaf-return LR names our temporary return site and is mapped back to the
  * displaced BL's return site. APSR and all other post-call regs stay exact. */
 SAVE
 ldr r0,[sp,#20]
 adr r1,.Lspi_return
 orr r1,r1,#1
 cmp r0,r1
 bne 1f
 ldr r0,=__gp_display_continue
 str r0,[sp,#20]
1:
 RESTORE
 ldr pc,=__gp_display_continue
.size ks37_panel_display_hook,.-ks37_panel_display_hook

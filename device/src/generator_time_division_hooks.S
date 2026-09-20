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
.section .text.ks37_time_division_request_hook,"ax",%progbits
.global ks37_time_division_request_hook
.type ks37_time_division_request_hook,%function
.thumb_func
ks37_time_division_request_hook:
 /* Original11ee8 stores C49 and C54, returns r3=1 and MOVS flags.
  * The callback observes the request and replays those two byte stores in
  * the same critical section. Caller registers/flags are otherwise exact. */
 sub sp,#8
 SAVE
 mrs r12,PRIMASK
 str r12,[sp,#32]
 cpsid i
 bl ks37_time_division_request
 ldr r12,[sp,#32]
 msr PRIMASK,r12
 RESTORE
 add sp,#8
 movs r3,#1
 bx lr
.size ks37_time_division_request_hook,.-ks37_time_division_request_hook

.section .text.ks37_time_division_applied_hook,"ax",%progbits
.global ks37_time_division_applied_hook
.type ks37_time_division_applied_hook,%function
.thumb_func
ks37_time_division_applied_hook:
 /* Replace only STRB at12cd2, then replay the exact stores12cd2..12cde
  * and continue12ce0. r4=C,r5=phase,r6=reload,r7=newdivision. CPU IRQ
  * exclusion keeps the observed native update coherent, not the timer CNT
  * frozen. No fake phase, block, note release or bar publication is made. */
 sub sp,#8
 SAVE
 mrs r12,PRIMASK
 str r12,[sp,#32]
 cpsid i
 strb.w r7,[r4,#0x48]
 ldr r3,=0x200054bc
 ldr r2,[r3]
 str r5,[r2,#0x24]
 str r6,[r2,#0x2c]
 str r6,[r3,#0xc]
 str r2,[sp,#8]
 str r3,[sp,#12]
 mov r0,r4
 mov r1,r7
 mov r2,r5
 mov r3,r6
 bl ks37_time_division_applied
 ldr r12,[sp,#32]
 msr PRIMASK,r12
 RESTORE
 add sp,#8
 ldr pc,=ks37_time_division_applied_continue
.size ks37_time_division_applied_hook,.-ks37_time_division_applied_hook

.syntax unified
.thumb
.section .text.boot_ram_hook,"ax",%progbits
.global boot_ram_hook
.type boot_ram_hook,%function
.thumb_func
boot_ram_hook:
    /* d33a BL entry: preserve incoming registers/flags and exact return link.
     * Ten words retain8-byte stack alignment. C runs before stock constructors. */
    push.w {r0-r7,r12,lr}
    mrs r4, apsr
    add r0, sp, #40
    bl boot_ram_target_initialize
    msr apsr_nzcvq, r4
    pop.w {r0-r7,r12,lr}
    b.w stock_runtime_init
.size boot_ram_hook,.-boot_ram_hook

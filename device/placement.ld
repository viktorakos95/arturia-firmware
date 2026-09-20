ENTRY(boot_ram_hook)
stock_runtime_init = 0x801d8c9;
ks37_control_pass = 0x8004935;
ks37_control_consume = 0x8004987;
__bda_stock_debounce = 0x801a761;
ks37_playback_transport_continue = 0x8012339;
ks37_playback_stock_step = 0x8013e8d;
__gd_stock_service = 0x80129cd;
__gp_writer_continue = 0x800d273;
__gp_spi_send = 0x800a66f;
__gp_display_continue = 0x801d4ed;
ks37_generator_pitch_native = 0x801c385;
ks37_time_division_applied_continue = 0x8012ce1;

__boot_stack_margin = 4096;
SECTIONS {
 .text 0x0801f400 : { KEEP(*(.text.boot_ram_hook)) *(.text*)
  . = ALIGN(4); __gd_rodata_start = .; *(.rodata*) __gd_rodata_end = .; }
 .boot_ram 0x20005eb0 (NOLOAD) : {
  KEEP(*(.boot_ram.header))
  . = ALIGN(8); KEEP(*(.boot_ram.guard_low))
  __boot_payload_start = .; KEEP(*(.boot_ram.payload)) __boot_payload_end = .;
  . = ALIGN(8); KEEP(*(.boot_ram.guard_high)) __boot_ram_end = .;
 }
 .unexpected_ram : { *(.data*) *(.bss*) *(COMMON) }
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
 ASSERT(SIZEOF(.unexpected_ram)==0,"Unowned RAM")
 ASSERT(__boot_payload_start==0x20005ef0,"Boot layout differs")
 ASSERT(__boot_ram_end+__boot_stack_margin<=0x2000c000,"RAM geometry exceeded")
 ASSERT(ADDR(.text)+SIZEOF(.text)<=0x0802fc00,"Code extent exceeded")
}

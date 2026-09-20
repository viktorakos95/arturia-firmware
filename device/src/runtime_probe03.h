#ifndef KS37_RUNTIME_PROBE03_H
#define KS37_RUNTIME_PROBE03_H
#include "device_runtime.h"
#include "boot_ram.h"
/* Diagnostic probe only. No gesture activation, MIDI injection, new sequence
 * pointer or custom DMA submission. Existing palette writer calls are reused. */
uint32_t ks37_probe_color(const Ks37Runtime *,const volatile BootRam *,
                          uint32_t receiver,uint32_t index,uint32_t original);
void ks37_probe_pixel(uint32_t receiver,uint32_t index,uint32_t original);
#endif

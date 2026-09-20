#ifndef KS37_INPUT_DEVICE_H
#define KS37_INPUT_DEVICE_H
#include <stdint.h>
#include "input_history.h"

/* New module; frozen probe03 sources are not modified. This profile names the
 * pinned stock USART/USB register layout, not a silicon identification. */
enum { ID_PROFILE_UNKNOWN=0, ID_PROFILE_STOCK_RM0008=1 };
enum { ID_F_COLD=1, ID_F_PROFILE=2, ID_F_USB_DROP=4, ID_F_UART=8,
       ID_F_USB_LIFE=16, ID_F_FRAME=32, ID_F_REENTRY=64, ID_F_EPOCH=128 };
typedef struct {
    uint32_t profile, faults, frame_epoch, consumed_epoch, down_lo;
    uint32_t uart_status_seen, usb_flags_seen, usb_calls, losses;
    uint8_t down_hi, cold_used, active, acquiring, frame_valid, lost_sources;
    uint8_t usb_started, reserved;
} InputDeviceState;
typedef struct {
    uint32_t gate, object, vtable, usb_self, din_self, usb_count, din_count;
    uint32_t usb_tag, din_tag, usb_write, usb_read, din_write, din_read;
    uint32_t scanner_source, scanner_notes[6];
    uint8_t parser[12], scanner_state[37];
} InputDeviceCold;
typedef struct {
    uint32_t object, vtable, buffer, length, output_port, output_shift;
    uint32_t input_port, input_shift, scanner_source, heap_begin, heap_end;
    uint8_t raw[10];
} InputDeviceFrame;

void input_device_init(InputDeviceState *, unsigned profile);
/* Called once at real cold M publication, under IRQ serialization. A failed
 * attempt cannot later be retried as a fresh lifetime. Returns three baseline
 * effects only on success. Never heals a runtime loss. */
int input_device_cold(InputDeviceState *, IhState *, const InputDeviceCold *, IhEffects effects[3]);
int input_device_loss(InputDeviceState *, IhState *, unsigned sources,
                      unsigned reason, IhEffects *);
int input_device_uart(InputDeviceState *, IhState *, uint32_t receiver,
                       uint32_t status, IhEffects *);
int input_device_usb_flags(InputDeviceState *, IhState *, uint32_t receiver,
                            uint32_t flags, unsigned initial_pristine, IhEffects *);
int input_device_usb_deinit(InputDeviceState *, IhState *, uint32_t receiver,
                            unsigned initial_pristine, IhEffects *);
/* Computes the exact stock CIN byte count; does not reject partial MIDI parser
 * state or impose a pending/admission policy. */
int input_device_usb_expected(const uint8_t *, unsigned length, unsigned *count);
int input_device_usb_result(InputDeviceState *, IhState *, uint32_t before,
    uint32_t after, uint32_t read_before, uint32_t read_after,
    unsigned expected, unsigned shape_ok, unsigned native_result, IhEffects *);
void input_device_frame_begin(InputDeviceState *);
int input_device_frame_complete(InputDeviceState *, const InputDeviceFrame *);
void input_device_scan_complete(InputDeviceState *, uint32_t scanner);
int input_device_physical_known(const InputDeviceState *, uint32_t batch_epoch);
int input_device_physical_empty(const InputDeviceState *, uint32_t batch_epoch);

/* Strong future runtime binding: no default implementation. Called with IRQs
 * masked. Runtime owns state/history and delivers ALL returned effects before
 * leaving the same serialized transaction. heap_end is current allowed break;
 * readiness includes boot/profile/linker coverage validation for this image. */
typedef struct {
    InputDeviceState *state;
    IhState *history;
    void *context;
    void (*deliver)(void *, const IhEffects *);
    uint32_t heap_begin, heap_end;
    unsigned ready;
} InputDeviceBinding;
extern int ks37_input_device_bind(InputDeviceBinding *);

void ks37_input_cold_entry(uint32_t object);
uint32_t ks37_input_usb_ingress_entry(uint32_t object, const uint8_t *packet, uint32_t length);
void ks37_input_uart_status_entry(uint32_t receiver, uint32_t status);
void ks37_input_usb_flags_entry(uint32_t receiver, uint32_t flags);
void ks37_input_usb_deinit_entry(uint32_t receiver);
void ks37_input_acquire_begin_entry(uint32_t receiver);
void ks37_input_acquire_end_entry(uint32_t receiver);
void ks37_input_scan_end_entry(uint32_t receiver);
/* Architectural Cortex-M register only; no vendor IDCODE read. */
uint32_t ks37_input_cpuid_read(void);
#endif

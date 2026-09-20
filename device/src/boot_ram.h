#ifndef KS37_BOOT_RAM_H
#define KS37_BOOT_RAM_H
#include <stdint.h>

enum { BOOT_RAM_START=0x20005eb0u, BOOT_INITIAL_SP=0x2000c000u,
       BOOT_GUARD_WORDS=4, BOOT_GUARD_BYTES=16 };
typedef enum { BOOT_COLD=0, BOOT_INITIALIZING, BOOT_READY, BOOT_FAILED } BootPhase;
enum { BOOT_F_LAYOUT=1u, BOOT_F_STATE=2u, BOOT_F_CONTEXT=4u,
       BOOT_F_SPACE=8u, BOOT_F_INIT=16u, BOOT_F_CANARY=32u,
       BOOT_F_BREAK=64u, BOOT_F_INIT_ALLOC=128u };
typedef struct {
    uint32_t phase, faults, published;
    uint32_t payload_address, payload_capacity, reserved_end, stack_margin;
    uint32_t minimum_sampled_msp, maximum_sampled_break, probes, denied_growth;
} BootRam;
typedef struct {
    uint32_t payload_address, payload_capacity, reserved_end, stack_margin;
} BootLayout;
typedef struct { uint32_t msp, control, ipsr, break_value, heap_origin, free_head; } BootObservation;
typedef struct {
    BootRam *state;
    uint32_t *guard_low;
    void *payload;
    uint32_t *guard_high;
} BootRamView;
typedef int (*BootPayloadInit)(void *, uint32_t);

/* Layout describes an explicit BSS extension and matching stock initial-break
 * patch, not a discovered free-RAM region. stack_margin is a policy parameter;
 * a sampled SP and margin do not prove future maximum stack/IRQ depth. */
int boot_ram_layout(uint32_t payload_bytes, uint32_t stack_margin, BootLayout *);
/* Called once after Reset zeroing, before constructors, with IRQs serialized.
 * View pointers must denote disjoint linker-owned storage matching the layout.
 * A failed init never publishes a payload; it never gives RAM back to stock.
 * init must not allocate or expose its payload before returning success. */
int boot_ram_initialize(BootRamView, const BootLayout *, BootObservation, BootPayloadInit);
/* Point-in-time health probe; failure latches until Reset. Never recovery by
 * seeing a good sample later. Does not claim to catch an unsampled stack burst. */
int boot_ram_probe(BootRamView, const BootLayout *, BootObservation);
void *boot_ram_payload(BootRamView, const BootLayout *, BootObservation);
/* Exact sbrk-like delta check. Caller serializes snapshot/check/store and sets
 * stock errno on rejection. Even failed add-on admission retains reservation.
 * Does not serialize the stock allocator's free-list operations. */
int boot_ram_growth(BootRam *, const BootLayout *, BootObservation,
                    int32_t delta, uint32_t *old_break, uint32_t *new_break);

/* Target exports, defined in boot_ram_target.c. Strong init callback must be
 * supplied by the real runtime; there is no weak success or dummy getter. */
int boot_payload_init(void *payload, uint32_t capacity);
void *boot_payload_get(void);
/* Payload remains owned and obtainable after a later fault, so runtime can
 * retain owners and perform cleanup. Check sticky status before new work. */
const volatile BootRam *boot_ram_status_get(void);
void boot_ram_target_initialize(uint32_t entry_msp);
uint32_t boot_ram_target_sbrk(int32_t delta);
#endif

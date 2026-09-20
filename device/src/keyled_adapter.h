#ifndef KEYLED_ADAPTER_H
#define KEYLED_ADAPTER_H
#include <stdint.h>

enum { KL_PIXELS = 41, KL_SAMPLES = 1184, KL_PADDING = 100 };
typedef enum { KL_PROFILE_UNKNOWN = 0, KL_PROFILE_RM0008 = 1 } KLProfile;
typedef enum {
    KL_PASS_STOCK = 0, KL_IDLE = 1, KL_STARTED = 2, KL_BUSY = 3,
    KL_FROZEN = 4, KL_BAD_ARGUMENT = 5
} KLResult;

typedef struct {
    uint16_t frame[KL_SAMPLES];
    uint32_t started, completed, magic;
    uint8_t active, in_flight, valid, frozen;
} KLState;

/* Bit i owns logical position i, packed byte order matches d26e. NULL means
 * no overlay. Caller owns this immutable input until kl_refresh returns. */
typedef struct { uint32_t color[KL_PIXELS], mask[2]; } KLOverlay;

/* Only before this state/frame has ever been submitted, or after MCU reset.
 * The caller must reserve this entire object outside stock/heap/stack areas.
 * Reinitializing a DMA-owned object is forbidden, including after a fault. */
void kl_init(KLState *state);

/* Full replacement for refresh d1f8, called in thread mode. KL_PASS_STOCK
 * requires executing its original entry through an unpatched trampoline.
 * UNKNOWN always passes before activation; after activation it freezes.
 * RM0008 requires independent identification of the actual MCU/DMA profile;
 * matching register addresses and H.ready DO NOT establish that profile.
 * Fault/error paths retain frame ownership until reset; no implicit abort.
 * Caller/patch set must exclude other submitters of this reserved frame. */
KLResult kl_refresh(KLState *state, const KLOverlay *overlay,
                    uint32_t receiver, KLProfile profile);

/* Platform operations supplied by keyled_mmio.c on ARM. Host tests supply
 * deterministic MMIO, stock-start and PRIMASK fakes instead. */
uint32_t kl_hw_ipsr(void);
uint32_t kl_hw_irq_save(void);
void kl_hw_irq_restore(uint32_t saved);
void kl_hw_barrier(void);
uint32_t kl_hw_read32(uint32_t address);
uint8_t kl_hw_read8(uint32_t address);
void kl_hw_write32(uint32_t address, uint32_t value);
volatile const uint16_t *kl_hw_stock_wave(void);
uint32_t kl_hw_stock_start(uint32_t source);
#endif

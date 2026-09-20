#include "keyled_adapter.h"

#define K 0x200013fcu
#define H 0x200055a8u
#define D 0x20005570u
#define TIMER 0x40012c00u
#define CHANNEL 0x4002001cu
#define ISR 0x40020000u
#define IFCR 0x40020004u
#define CONFIG 0x590u
#define MAGIC 0x4b4c4431u

void kl_init(KLState *s) {
    if (!s) return;
    s->started = s->completed = 0;
    s->active = s->in_flight = s->valid = s->frozen = 0;
    s->magic = MAGIC;
}

static KLResult reject(KLState *s) {
    if (!s->active) return KL_PASS_STOCK;
    s->frozen = 1;
    return KL_FROZEN;
}

/* Only this compact snapshot holds PRIMASK. Encoding/validation is outside
 * the critical section; the released destination is private to this call. */
static int snapshot(KLState *s) {
    volatile const uint16_t *stock = kl_hw_stock_wave();
    int changed = !s->valid;
    unsigned valid = s->valid;
    for (unsigned i = 0; i < KL_SAMPLES; ++i) {
        uint16_t value = stock[i];
        changed |= valid && s->frame[i] != value;
        s->frame[i] = value;
    }
    s->valid = 0; /* This candidate is not yet the accepted/published frame. */
    return changed;
}

static int compose(KLState *s, const KLOverlay *o, int *changed) {
    unsigned i, pixel, component, bit;
    for (i = 0; i < KL_PADDING; ++i) {
        unsigned tail = KL_PADDING + 24 * KL_PIXELS + i;
        if (s->frame[i] || s->frame[tail]) return 0;
    }
    for (pixel = 0; pixel < KL_PIXELS; ++pixel) {
        int owns = o && (o->mask[pixel >> 5] & (1u << (pixel & 31)));
        for (component = 0; component < 3; ++component) {
            unsigned shift = component == 0 ? 8 : (component == 1 ? 0 : 16);
            for (bit = 0; bit < 8; ++bit) {
                uint16_t desired;
                i = KL_PADDING + pixel * 24 + component * 8 + bit;
                desired = s->frame[i];
                if (desired != 30 && desired != 60) return 0;
                if (owns) desired = o->color[pixel] & (1u << (shift + 7 - bit)) ? 60 : 30;
                *changed |= s->frame[i] != desired;
                s->frame[i] = desired;
            }
        }
    }
    return 1;
}

static KLResult ready(KLState *s, uint32_t source, uint32_t *ccr_out) {
    uint32_t ccr, remaining, memory, peripheral, status;
    uint8_t hstate, dstate, lock;
    if ((source & 3u) || kl_hw_read32(H) != TIMER ||
        kl_hw_read32(H + 0x20) != D || kl_hw_read32(D) != CHANNEL ||
        kl_hw_read32(D + 0x24) != H) return reject(s);
    ccr = kl_hw_read32(CHANNEL);
    remaining = kl_hw_read32(CHANNEL + 4);
    peripheral = kl_hw_read32(CHANNEL + 8);
    memory = kl_hw_read32(CHANNEL + 12);
    status = kl_hw_read32(ISR);
    hstate = kl_hw_read8(H + 0x39);
    dstate = kl_hw_read8(D + 0x21);
    lock = kl_hw_read8(D + 0x20);
    if ((ccr & ~15u) != CONFIG || remaining > KL_SAMPLES ||
        ((s->started || s->in_flight) && (memory != source || peripheral != TIMER + 0x34)) ||
        (!(s->started || s->in_flight) && ((memory && memory != K + 4) ||
                       (peripheral && peripheral != TIMER + 0x34)))) {
        return reject(s);
    }
    /* Never turn a TE or ambiguous ownership into successful completion.
     * D.error is deliberately ignored: it accumulates historical errors. */
    if ((status & 0x80u) || dstate == 4) return reject(s);
    if (remaining || hstate != 1 || dstate != 1 || lock) {
        return s->active ? KL_BUSY : KL_PASS_STOCK;
    }
    if (s->started && !s->in_flight && (ccr & 1u)) {
        return reject(s);
    }
    *ccr_out = ccr;
    return KL_IDLE; /* Internal meaning: validated normal idle/completion. */
}

KLResult kl_refresh(KLState *s, const KLOverlay *o, uint32_t receiver, KLProfile profile) {
    uint32_t saved, ccr, source;
    KLResult result;
    int changed, was_active;
    if (!s) return KL_PASS_STOCK;
    if (s->magic != MAGIC) {
        return s->active || s->in_flight || s->frozen ? KL_FROZEN : KL_PASS_STOCK;
    }
    if (receiver != K) return reject(s);
    if (profile != KL_PROFILE_RM0008) return reject(s);
    if (s->frozen) return KL_FROZEN;
    if (o && (o->mask[1] & ~0x1ffu)) return s->active ? KL_BAD_ARGUMENT : KL_PASS_STOCK;
    /* Known d1f8 callers are main/service and startup animation. Refuse an
     * unexpected exception context rather than snapshot a suspended writer. */
    if (kl_hw_ipsr()) return s->active ? KL_BUSY : KL_PASS_STOCK;
    saved = kl_hw_irq_save();
    source = (uint32_t)(uintptr_t)s->frame;
    result = ready(s, source, &ccr);
    if (result != KL_IDLE) goto done;
    /* RM0008 normal-mode proof: count is post-decremented after read/write;
     * count=0 serves no more requests. EN readback additionally confirms the
     * channel can be reprogrammed. This is NOT an abort/TE release proof. */
    kl_hw_write32(CHANNEL, ccr & ~1u);
    kl_hw_barrier();
    if (kl_hw_read32(CHANNEL) & 1u) {
        result = s->active ? KL_BUSY : KL_PASS_STOCK; goto done;
    }
    if (s->in_flight) {
        s->in_flight = 0;
        if (s->completed != UINT32_MAX) ++s->completed;
    }
    was_active = s->active;
    changed = snapshot(s);
    /* Reentrant IRQ refreshes must now consume BUSY, including before the
     * first own start. The runtime must not hold an outer PRIMASK here. */
    s->active = 1;
    kl_hw_irq_restore(saved);
    if (!compose(s, o, &changed)) {
        if (!was_active) { s->active = 0; return KL_PASS_STOCK; }
        return reject(s);
    }
    if (!changed) { s->valid = 1; return KL_IDLE; }
    saved = kl_hw_irq_save();
    result = ready(s, source, &ccr);
    if (result != KL_IDLE) goto done; /* valid remains0: retry cannot lose it. */
    if (ccr & 1u) { result = reject(s); goto done; }
    s->in_flight = 1;
    /* Remove flags from the previous transaction before arming the stock
     * callbacks. A stale pending NVIC exception then has no stale DMA flags. */
    kl_hw_write32(IFCR, 0xf0u);
    kl_hw_barrier();
    if (kl_hw_stock_start(source) != 0 ||
        kl_hw_read32(CHANNEL + 12) != source ||
        kl_hw_read32(CHANNEL + 8) != TIMER + 0x34 ||
        (kl_hw_read32(CHANNEL) & ~15u) != CONFIG ||
        !(kl_hw_read32(CHANNEL) & 1u) ||
        kl_hw_read32(CHANNEL + 4) > KL_SAMPLES ||
        kl_hw_read8(D + 0x20) != 1 || kl_hw_read8(D + 0x21) != 2 ||
        kl_hw_read8(H + 0x39) != 2 || (kl_hw_read32(ISR) & 0x80u)) {
        result = reject(s); goto done;
    }
    if (s->started != UINT32_MAX) ++s->started;
    s->valid = 1;
    result = KL_STARTED;
done:
    kl_hw_irq_restore(saved);
    return result;
}

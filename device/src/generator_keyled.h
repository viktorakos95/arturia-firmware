#ifndef KS37_GENERATOR_KEYLED_H
#define KS37_GENERATOR_KEYLED_H
#include <stdint.h>

/* Presentation through the existing native pixel writer. This is a CPU
 * shadow, NOT a DMA source or a frame publisher. The caller holds its preserved
 * PRIMASK critical section through each request and the corresponding native
 * write. DMA can observe an intermediate frame, as on the stock live buffer.
 *
 * Zero-initialize once with the runtime, before stock K construction. Every
 * actual stock request at d26e is observed, including construction. All 41
 * positions must have been observed before GEN takes any keys. No synthetic
 * black/bootstrap colors earn the baseline. Position 0 belongs to the existing
 * Chord adapter; position 40 has no established physical role and stays stock.
 * Logical positions 2..38 / first 16 at 2..17 still require physical checking.
 */
enum { GK_PIXELS=41, GK_FIRST_KEY=2, GK_KEYS=37, GK_STEPS=16,
       GK_HIT=0x0c0c00, GK_CURSOR=0x002a00, GK_HIT_CURSOR=0x082a00,
       GK_CAPTURE=0x00152a };
typedef struct {
    uint32_t stock[GK_PIXELS], last[GK_PIXELS], seen[2];
} GenKeyledState;
typedef struct {
    const uint8_t *notes; /* coherent, caller-owned capture pitches, at most32 */
    uint16_t hits;       /* applied CURRENT only; never the private projection */
    int16_t lower;      /* effective native lower pitch, read without mutation */
    int8_t step;        /* -1 stopped/no established step; pause keeps last */
    uint8_t active, capturing, count, range_valid, fault;
    uint8_t valid_steps, page; /* projected CURRENT page,0..16 valid positions */
} GenKeyledInput;

/* Pure page projection. Invalid/unestablished cursor uses page0 without a
 * cursor; only valid CURRENT.length1..64 earns visible step positions. */
void gk_project_steps(GenKeyledInput *,uint64_t active,unsigned length,int global_step);
unsigned gk_baseline_ready(const GenKeyledState *);
/* Records only a genuine stock request. Result replaces packedColor in the
 * same call. Invalid indices pass unchanged without touching state. */
uint32_t gk_stock_pixel(GenKeyledState *,const GenKeyledInput *,
                      unsigned index,uint32_t original);
/* Commits an intended changed output to last[]. A true result MUST be followed
 * immediately by the native writer under the same critical section. Generated
 * writes bypass gk_stock_pixel, keeping the latest native intention intact. */
unsigned gk_output_due(GenKeyledState *,const GenKeyledInput *,
                       unsigned index,uint32_t *color);
#endif

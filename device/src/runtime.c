#include <stddef.h>
#include <stdint.h>
/* Compiler ABI helper, private to the future adapter. Volatile stores keep the
 * compiler from replacing the implementation with a recursive memcpy call. */
void __aeabi_memcpy(void *to, const void *from, size_t length) {
    volatile uint8_t *d = to;
    const uint8_t *s = from;
    while (length--) *d++ = *s++;
}
void __aeabi_memcpy8(void *to, const void *from, size_t length) {
    __aeabi_memcpy(to,from,length);
}

/* Cortex-M3 EABI uses r0:r1 for uint64 and r2 for the shift count. The
 * firmware image is little endian. Use only 32-bit operations here so these
 * helpers cannot compile into recursive calls to themselves. */
typedef union {uint64_t wide;struct {uint32_t low,high;} words;} Wide;
uint64_t __aeabi_llsl(uint64_t value,unsigned count) {
    Wide in={.wide=value},out;
    if(!count)return value;
    if(count<32) {
        out.words.low=in.words.low<<count;
        out.words.high=(in.words.high<<count)|(in.words.low>>(32-count));
    }else if(count<64) {
        out.words.low=0;out.words.high=in.words.low<<(count-32);
    }else out.words.low=out.words.high=0;
    return out.wide;
}
uint64_t __aeabi_llsr(uint64_t value,unsigned count) {
    Wide in={.wide=value},out;
    if(!count)return value;
    if(count<32) {
        out.words.high=in.words.high>>count;
        out.words.low=(in.words.low>>count)|(in.words.high<<(32-count));
    }else if(count<64) {
        out.words.high=0;out.words.low=in.words.high>>(count-32);
    }else out.words.low=out.words.high=0;
    return out.wide;
}

/* The compiler emits this two-argument helper for zero-initialized structures.
 * The 4 suffix permits aligned input; byte stores also handle a short tail. */
void __aeabi_memclr4(void *to, size_t length) {
    volatile uint8_t *d = to;
    while (length--) *d++ = 0;
}

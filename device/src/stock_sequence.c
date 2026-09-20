#include "stock_sequence.h"

int stock_sequence_encode(uint8_t out[STOCK_SEQUENCE_BYTES],
                          const GenSnapshot *snapshot,
                          const uint8_t header[8]) {
    uint8_t saved_header[7];
    unsigned step, voice, i;
    if (!out || !snapshot || !header) return 0;
    unsigned length=snapshot->params.length;
    if(length<1 || length>64) return 0;
    uint64_t length_mask=length==64?UINT64_MAX:(UINT64_C(1)<<length)-1u;
    if((snapshot->active & ~snapshot->known) || (snapshot->active & ~length_mask)) return 0;
    for (step = 0; step < length; ++step) {
        if (!(snapshot->active & (UINT64_C(1) << step))) continue;
        if (snapshot->cells[step].pitch > 127 ||
            snapshot->cells[step].velocity == 0 ||
            snapshot->cells[step].velocity > 127) return 0;
    }
    for (i = 0; i < 7; ++i) saved_header[i] = header[i + 1];
    for (step = 0; step < 64; ++step) {
        for (voice = 0; voice < 8; ++voice) {
            unsigned at = 16 * step + 2 * voice;
            out[at] = 0xff;
            out[at + 1] = 0x40;
        }
    }
    for (step = 0; step < length; ++step) {
        if (snapshot->active & (UINT64_C(1) << step)) {
            out[16 * step] = snapshot->cells[step].pitch;
            out[16 * step + 1] = snapshot->cells[step].velocity;
        } else {
            out[16 * step] = 0x82;
        }
    }
    out[STOCK_SEQUENCE_HEADER] = (uint8_t)length;
    for (i = 0; i < 7; ++i) out[STOCK_SEQUENCE_HEADER + i + 1] = saved_header[i];
    return 1;
}

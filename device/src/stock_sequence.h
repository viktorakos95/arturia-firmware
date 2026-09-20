#ifndef KS37_STOCK_SEQUENCE_H
#define KS37_STOCK_SEQUENCE_H
#include "generator_core.h"

enum { STOCK_SEQUENCE_BYTES = 0x408, STOCK_SEQUENCE_HEADER = 0x400 };

/* Pure byte codec, NOT device publication. Encode snapshot.params.length (1..64) monophonic steps and
 * terminate every remaining voice. header[0..7] corresponds to +0x400..407:
 * length is replaced by snapshot.params.length; the other seven bytes are copied without guessing
 * their meaning or setting stock dirty/persistence flags. The caller must
 * choose/snapshot a valid header separately. No address or timing is chosen.
 *
 * out must own 1032 bytes and must not overlap snapshot. header may be out's
 * header. Active cells require known=1, pitch<=127, velocity in 1..127.
 * Return 0 for invalid arguments/input; out remains unchanged. */
int stock_sequence_encode(uint8_t out[STOCK_SEQUENCE_BYTES],
                          const GenSnapshot *snapshot,
                          const uint8_t header[8]);
#endif

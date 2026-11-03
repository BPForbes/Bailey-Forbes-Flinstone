#ifndef FL_KQ_HISTORY_BUNDLE_H
#define FL_KQ_HISTORY_BUNDLE_H

/*
 * History bundle (FL-04): what a KeyQuorum history download carries. Flinstone's own framing,
 * not a KeyQuorum or sealed-envelope format; KeyQuorum never parses it and the relay of it
 * (FL_NET_SESSION_OP_KQH_*) only moves its bytes.
 *
 * Layout: one count byte (always 2), then twice { u32 big-endian length, that many bytes }.
 * The first letter is the sealed file-history letter (KQPB kind 15), the second the history
 * snapshot letter (kind 17). This module checks the framing only; it opens neither letter and
 * does not look at their magic or kind byte.
 */

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#define FL_HISTORY_BUNDLE_MAX_LETTERS 2u
/** Proposed bound, not measured against real histories. */
#define FL_HISTORY_BUNDLE_MAX_BYTES (1u << 20)

/** Frame two non-empty letters into out[cap]. INVAL for a bad argument or an empty letter;
 *  NOMEM when the bundle does not fit cap; INVAL when it would exceed FL_HISTORY_BUNDLE_MAX_BYTES. */
fl_result_t fl_history_bundle_pack(uint8_t *out, size_t cap, size_t *out_len,
                                   const uint8_t *l15, size_t n15, const uint8_t *l17, size_t n17);

/** Split a bundle into views of its two letters (no copy; they point into in). Refuses a wrong
 *  count, an empty or overlong letter, a length that runs past the end, an input over
 *  FL_HISTORY_BUNDLE_MAX_BYTES and any trailing byte. Outputs are untouched on failure. */
fl_result_t fl_history_bundle_unpack(const uint8_t *in, size_t len, const uint8_t **l15, size_t *n15,
                                     const uint8_t **l17, size_t *n17);

#endif /* FL_KQ_HISTORY_BUNDLE_H */

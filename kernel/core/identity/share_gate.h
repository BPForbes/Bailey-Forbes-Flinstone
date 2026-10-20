/**
 * Pre-send gate for server file sharing (docs/FILE_SHARING_KEYQUORUM.md,
 * requirement 12). Run by the sender before an offer leaves: the receiver must
 * be an account this store knows, with a KeyQuorum key, on the sender's own
 * line (self, ancestor or descendant) or joined to the sender by a signed,
 * active bridge. The server does not repeat the check.
 */
#ifndef FL_SHARE_GATE_H
#define FL_SHARE_GATE_H

#include "user_db.h"

typedef enum {
    FL_SHARE_GATE_OK = 0,
    /** The sending account is unknown or has no heritage key id. */
    FL_SHARE_GATE_NO_SENDER,
    /** The receiver is not an account in this store. */
    FL_SHARE_GATE_NO_RECEIVER,
    /** The receiver has no KeyQuorum key yet (it gets one at its next login). */
    FL_SHARE_GATE_NO_RECEIVER_KEY,
    /** Another branch, and no signed, active bridge joins the two accounts. */
    FL_SHARE_GATE_NO_BRIDGE,
} fl_share_gate_result_t;

fl_share_gate_result_t fl_share_gate_check(const fl_user_db_t *users, const char *sender,
                                           const char *receiver);

/** A plain sentence for a refusal, naming no secret. */
const char *fl_share_gate_reason(fl_share_gate_result_t result);

#endif /* FL_SHARE_GATE_H */

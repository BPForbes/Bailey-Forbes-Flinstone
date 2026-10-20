#include "share_gate.h"
#include "bridge.h"
#include "heritage.h"

fl_share_gate_result_t fl_share_gate_check(const fl_user_db_t *users, const char *sender,
                                           const char *receiver) {
    const fl_user_record_t *from = fl_user_db_find(users, sender);
    const fl_user_record_t *to = fl_user_db_find(users, receiver);

    if (!from || !fl_heritage_key_valid(from->key_id))
        return FL_SHARE_GATE_NO_SENDER;
    if (!to || !fl_heritage_key_valid(to->key_id))
        return FL_SHARE_GATE_NO_RECEIVER;
    if (!fl_user_db_signing_public(users, to->key_id))
        return FL_SHARE_GATE_NO_RECEIVER_KEY;
    switch (fl_heritage_relation(from->key_id, to->key_id)) {
    case FL_HERITAGE_REL_SELF:
    case FL_HERITAGE_REL_ANCESTOR:
    case FL_HERITAGE_REL_DESCENDANT:
        return FL_SHARE_GATE_OK;
    default:
        return fl_bridge_active_between(users, from->key_id, to->key_id) ? FL_SHARE_GATE_OK
                                                                         : FL_SHARE_GATE_NO_BRIDGE;
    }
}

const char *fl_share_gate_reason(fl_share_gate_result_t result) {
    switch (result) {
    case FL_SHARE_GATE_OK:
        return "allowed";
    case FL_SHARE_GATE_NO_SENDER:
        return "the sending account has no heritage key id";
    case FL_SHARE_GATE_NO_RECEIVER:
        return "the receiver is not an account on this machine";
    case FL_SHARE_GATE_NO_RECEIVER_KEY:
        return "the receiver has no KeyQuorum key yet; it gets one at its next login";
    case FL_SHARE_GATE_NO_BRIDGE:
        return "the receiver is on another branch and no signed, active bridge joins you";
    default:
        return "refused";
    }
}

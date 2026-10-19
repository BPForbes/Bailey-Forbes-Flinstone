/*
 * Account heritage key ids. The rules live in KeyQuorum's Rust crate
 * (third_party/keyquorum/ffi/identity, libkeyquorum_identity.a); these are the
 * thin C names the rest of the shell uses. Return codes map onto fl_result_t
 * as before: invalid input is INVAL, too deep or too long is NOMEM, and the
 * master account has no parent (NOENT).
 */
#include "heritage.h"
#include "keyquorum_identity.h"
#include <stdio.h>
#include <string.h>

static fl_result_t kq_to_result(int rc) {
    switch (rc) {
    case KQ_HERITAGE_OK:
        return FL_RESULT_OK;
    case KQ_HERITAGE_TOO_DEEP:
    case KQ_HERITAGE_NO_ROOM:
        return FL_RESULT_NOMEM;
    default:
        return FL_RESULT_INVAL;
    }
}

int fl_heritage_segment_valid(const char *name) {
    return kq_heritage_segment_valid(name) != 0;
}

int fl_heritage_name_available_shape(const char *name) {
    return kq_heritage_name_available(name) != 0;
}

int fl_heritage_key_valid(const char *key) {
    return kq_heritage_key_valid(key) != 0;
}

size_t fl_heritage_depth(const char *key) {
    return kq_heritage_depth(key);
}

const char *fl_heritage_leaf(const char *key) {
    const char *dot;

    if (!fl_heritage_key_valid(key))
        return NULL;
    dot = strrchr(key, FL_HERITAGE_SEP);
    return dot ? dot + 1 : key;
}

fl_result_t fl_heritage_child(const char *parent, const char *name, char *out, size_t cap) {
    return kq_to_result(kq_heritage_child(parent, name, out, cap));
}

fl_result_t fl_heritage_parent(const char *key, char *out, size_t cap) {
    if (!fl_heritage_key_valid(key))
        return FL_RESULT_INVAL;
    if (!strchr(key, FL_HERITAGE_SEP))
        return FL_RESULT_NOENT;
    return kq_to_result(kq_heritage_parent(key, out, cap));
}

int fl_heritage_is_ancestor_or_self(const char *a, const char *b) {
    return kq_heritage_is_ancestor_or_self(a, b) != 0;
}

fl_heritage_relation_t fl_heritage_relation(const char *a, const char *b) {
    return (fl_heritage_relation_t)kq_heritage_relation(a, b);
}

const char *fl_heritage_relation_name(fl_heritage_relation_t rel) {
    switch (rel) {
    case FL_HERITAGE_REL_SELF:
        return "self";
    case FL_HERITAGE_REL_ANCESTOR:
        return "ancestor";
    case FL_HERITAGE_REL_DESCENDANT:
        return "descendant";
    case FL_HERITAGE_REL_SIBLING:
        return "sibling";
    case FL_HERITAGE_REL_OTHER_BRANCH:
        return "other branch";
    default:
        return "invalid";
    }
}

fl_result_t fl_heritage_home_rel(const char *key, char *out, size_t cap) {
    return kq_to_result(kq_heritage_home_rel(key, out, cap));
}

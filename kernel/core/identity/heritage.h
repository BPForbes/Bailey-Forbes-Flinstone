/**
 * Account heritage key ids (contract_p2_heritage.h): parse, derive and compare
 * dotted labels such as Root.Flinstone.Bob. Pure functions, no storage.
 */
#ifndef FL_HERITAGE_H
#define FL_HERITAGE_H

#include "contract_result.h"
#include "contract_p2_heritage.h"
#include <stddef.h>

/** 1 when name is a usable account name / key id segment: [A-Za-z0-9_-], 1..31 chars. */
int fl_heritage_segment_valid(const char *name);

/** 1 when name may be given to a new account: a valid segment that is not "Root". */
int fl_heritage_name_available_shape(const char *name);

/** 1 when key is "Root" or "Root" followed by valid segments, at most FL_HERITAGE_MAX_DEPTH deep. */
int fl_heritage_key_valid(const char *key);

/** Number of segments ("Root" is 1); 0 for an invalid key. */
size_t fl_heritage_depth(const char *key);

/** Last segment (the account's own name); NULL for an invalid key. */
const char *fl_heritage_leaf(const char *key);

/** out = parent "." name. FL_RESULT_INVAL for a bad parent or name, NOMEM when too deep or long. */
fl_result_t fl_heritage_child(const char *parent, const char *name, char *out, size_t cap);

/** out = key without its last segment. FL_RESULT_NOENT for "Root". */
fl_result_t fl_heritage_parent(const char *key, char *out, size_t cap);

/** 1 when a is b or an ancestor of b (a may act for b). */
int fl_heritage_is_ancestor_or_self(const char *a, const char *b);

/** How b stands relative to a; FL_HERITAGE_REL_INVALID when either key is invalid. */
fl_heritage_relation_t fl_heritage_relation(const char *a, const char *b);

/** Short display word for a relation ("self", "descendant", ...). */
const char *fl_heritage_relation_name(fl_heritage_relation_t rel);

/**
 * out = home directory relative to the jail root: "home" for Root,
 * "home/Flinstone/Bob" for Root.Flinstone.Bob.
 */
fl_result_t fl_heritage_home_rel(const char *key, char *out, size_t cap);

#endif /* FL_HERITAGE_H */

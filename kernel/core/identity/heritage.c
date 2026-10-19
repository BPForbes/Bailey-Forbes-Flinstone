#include "heritage.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int segment_char_ok(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static int segment_span_valid(const char *s, size_t len) {
    size_t i;

    if (len == 0u || len > FL_HERITAGE_SEGMENT_MAX_CHARS)
        return 0;
    for (i = 0; i < len; i++) {
        if (!segment_char_ok(s[i]))
            return 0;
    }
    return 1;
}

int fl_heritage_segment_valid(const char *name) {
    if (!name)
        return 0;
    return segment_span_valid(name, strlen(name));
}

int fl_heritage_name_available_shape(const char *name) {
    return fl_heritage_segment_valid(name) && strcasecmp(name, FL_HERITAGE_ROOT_KEY) != 0;
}

size_t fl_heritage_depth(const char *key) {
    const char *p;
    size_t depth = 0;
    size_t root_len = strlen(FL_HERITAGE_ROOT_KEY);

    if (!key || strncmp(key, FL_HERITAGE_ROOT_KEY, root_len) != 0)
        return 0;
    if (key[root_len] != '\0' && key[root_len] != FL_HERITAGE_SEP)
        return 0;
    if (strlen(key) >= FL_HERITAGE_KEY_ID_MAX)
        return 0;
    depth = 1;
    p = key + root_len;
    while (*p == FL_HERITAGE_SEP) {
        const char *seg = p + 1;
        const char *end = strchr(seg, FL_HERITAGE_SEP);
        size_t len = end ? (size_t)(end - seg) : strlen(seg);

        if (!segment_span_valid(seg, len) ||
            (len == root_len && strncasecmp(seg, FL_HERITAGE_ROOT_KEY, len) == 0))
            return 0;
        depth++;
        if (depth > FL_HERITAGE_MAX_DEPTH)
            return 0;
        p = seg + len;
    }
    return *p == '\0' ? depth : 0;
}

int fl_heritage_key_valid(const char *key) {
    return fl_heritage_depth(key) > 0u;
}

const char *fl_heritage_leaf(const char *key) {
    const char *dot;

    if (!fl_heritage_key_valid(key))
        return NULL;
    dot = strrchr(key, FL_HERITAGE_SEP);
    return dot ? dot + 1 : key;
}

fl_result_t fl_heritage_child(const char *parent, const char *name, char *out, size_t cap) {
    int n;

    if (!out || cap == 0u)
        return FL_RESULT_INVAL;
    out[0] = '\0';
    if (!fl_heritage_key_valid(parent) || !fl_heritage_name_available_shape(name))
        return FL_RESULT_INVAL;
    if (fl_heritage_depth(parent) >= FL_HERITAGE_MAX_DEPTH)
        return FL_RESULT_NOMEM;
    n = snprintf(out, cap, "%s%c%s", parent, FL_HERITAGE_SEP, name);
    if (n < 0 || (size_t)n >= cap || (size_t)n >= FL_HERITAGE_KEY_ID_MAX) {
        out[0] = '\0';
        return FL_RESULT_NOMEM;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_heritage_parent(const char *key, char *out, size_t cap) {
    const char *dot;
    size_t len;

    if (!out || cap == 0u)
        return FL_RESULT_INVAL;
    out[0] = '\0';
    if (!fl_heritage_key_valid(key))
        return FL_RESULT_INVAL;
    dot = strrchr(key, FL_HERITAGE_SEP);
    if (!dot)
        return FL_RESULT_NOENT;
    len = (size_t)(dot - key);
    if (len >= cap)
        return FL_RESULT_NOMEM;
    memcpy(out, key, len);
    out[len] = '\0';
    return FL_RESULT_OK;
}

int fl_heritage_is_ancestor_or_self(const char *a, const char *b) {
    size_t alen;

    if (!fl_heritage_key_valid(a) || !fl_heritage_key_valid(b))
        return 0;
    alen = strlen(a);
    if (strncmp(a, b, alen) != 0)
        return 0;
    return b[alen] == '\0' || b[alen] == FL_HERITAGE_SEP;
}

fl_heritage_relation_t fl_heritage_relation(const char *a, const char *b) {
    char pa[FL_HERITAGE_KEY_ID_MAX];
    char pb[FL_HERITAGE_KEY_ID_MAX];

    if (!fl_heritage_key_valid(a) || !fl_heritage_key_valid(b))
        return FL_HERITAGE_REL_INVALID;
    if (strcmp(a, b) == 0)
        return FL_HERITAGE_REL_SELF;
    if (fl_heritage_is_ancestor_or_self(a, b))
        return FL_HERITAGE_REL_DESCENDANT;
    if (fl_heritage_is_ancestor_or_self(b, a))
        return FL_HERITAGE_REL_ANCESTOR;
    if (fl_heritage_parent(a, pa, sizeof pa) == FL_RESULT_OK &&
        fl_heritage_parent(b, pb, sizeof pb) == FL_RESULT_OK && strcmp(pa, pb) == 0)
        return FL_HERITAGE_REL_SIBLING;
    return FL_HERITAGE_REL_OTHER_BRANCH;
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
    size_t root_len = strlen(FL_HERITAGE_ROOT_KEY);
    size_t i;
    int n;

    if (!out || cap == 0u)
        return FL_RESULT_INVAL;
    out[0] = '\0';
    if (!fl_heritage_key_valid(key))
        return FL_RESULT_INVAL;
    n = snprintf(out, cap, "%s%s", FL_HERITAGE_HOME_DIR, key + root_len);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return FL_RESULT_NOMEM;
    }
    for (i = strlen(FL_HERITAGE_HOME_DIR); out[i]; i++) {
        if (out[i] == FL_HERITAGE_SEP)
            out[i] = '/';
    }
    return FL_RESULT_OK;
}

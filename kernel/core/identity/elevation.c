#include "elevation.h"
#include "user_db.h"
#include "timekeeping.h"
#include "mem_domain.h"
#include "fl/fl_stack_asm.h"
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct fl_elev_slot {
    fl_elevation_token_t token;
    char                 principal[FL_USER_NAME_MAX];
    int64_t              expires_ns;
    int                  used;
} fl_elev_slot_t;

_Static_assert(sizeof(fl_elev_slot_t) == 56u, "asm_fl_elev_count_active slot layout");
_Static_assert(offsetof(fl_elev_slot_t, expires_ns) == 40u, "asm_fl_elev_count_active expires_ns offset");
_Static_assert(offsetof(fl_elev_slot_t, used) == 48u, "asm_fl_elev_count_active used offset");

#define FL_ELEV_MAX_SLOTS 16u

static fl_elev_slot_t s_slots[FL_ELEV_MAX_SLOTS];
static fl_elevation_token_t s_next = 1u;
static pthread_mutex_t s_elev_mu = PTHREAD_MUTEX_INITIALIZER;

static fl_elevation_token_t elevation_alloc_token(void) {
    s_next++;
    if (s_next == FL_ELEVATION_TOKEN_NONE)
        s_next = 1u;
    return s_next;
}

fl_result_t fl_elevation_grant(const char *principal, const char *reason, fl_elevation_token_t *out) {
    int64_t now = 0;
    size_t i;
    (void)reason;

    if (!principal || !out)
        return FL_RESULT_INVAL;
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    pthread_mutex_lock(&s_elev_mu);
    for (i = 0; i < FL_ELEV_MAX_SLOTS; i++) {
        if (!s_slots[i].used || s_slots[i].expires_ns <= now) {
            if (s_slots[i].used)
                mem_domain_zero(&s_slots[i], sizeof(s_slots[i]));
            s_slots[i].used = 1;
            s_slots[i].token = elevation_alloc_token();
            (void)snprintf(s_slots[i].principal, sizeof(s_slots[i].principal),
                           "%s", principal);
            s_slots[i].expires_ns = now + (int64_t)FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS * 1000000000LL;
            *out = s_slots[i].token;
            pthread_mutex_unlock(&s_elev_mu);
            return FL_RESULT_OK;
        }
    }
    pthread_mutex_unlock(&s_elev_mu);
    return FL_RESULT_BUSY;
}

/* s_elev_mu is held. The slot of an active token, or NULL (an expired one is wiped here). */
static fl_elev_slot_t *active_slot_locked(fl_elevation_token_t token, int64_t now) {
    size_t i;

    if (token == FL_ELEVATION_TOKEN_NONE)
        return NULL;
    for (i = 0; i < FL_ELEV_MAX_SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].token == token) {
            if (s_slots[i].expires_ns > now)
                return &s_slots[i];
            mem_domain_zero(&s_slots[i], sizeof(s_slots[i]));
            return NULL;
        }
    }
    return NULL;
}

int fl_elevation_active(fl_elevation_token_t token) {
    int64_t now = 0;
    int ok;

    if (token == FL_ELEVATION_TOKEN_NONE)
        return 0;
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return 0;

    pthread_mutex_lock(&s_elev_mu);
    ok = active_slot_locked(token, now) != NULL;
    pthread_mutex_unlock(&s_elev_mu);
    return ok;
}

fl_result_t fl_elevation_refresh(fl_elevation_token_t token, int64_t *expires_ns_out) {
    fl_elev_slot_t *slot;
    int64_t now = 0;
    fl_result_t rc = FL_RESULT_NOENT;

    /* The clock is read under the lock: a refresh that read an earlier time and locked later would
     * otherwise set back a window another refresh has already moved on. */
    pthread_mutex_lock(&s_elev_mu);
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK) {
        pthread_mutex_unlock(&s_elev_mu);
        return FL_RESULT_ERR;
    }
    slot = active_slot_locked(token, now);
    if (slot) {
        slot->expires_ns = now + (int64_t)FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS * 1000000000LL;
        if (expires_ns_out)
            *expires_ns_out = slot->expires_ns;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_elev_mu);
    return rc;
}

fl_result_t fl_elevation_expires(fl_elevation_token_t token, int64_t *expires_ns_out) {
    fl_elev_slot_t *slot;
    int64_t now = 0;
    fl_result_t rc = FL_RESULT_NOENT;

    if (!expires_ns_out)
        return FL_RESULT_INVAL;
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    pthread_mutex_lock(&s_elev_mu);
    slot = active_slot_locked(token, now);
    if (slot) {
        *expires_ns_out = slot->expires_ns;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_elev_mu);
    return rc;
}

void fl_elevation_revoke(fl_elevation_token_t token) {
    size_t i;
    pthread_mutex_lock(&s_elev_mu);
    for (i = 0; i < FL_ELEV_MAX_SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].token == token)
            mem_domain_zero(&s_slots[i], sizeof(s_slots[i]));
    }
    pthread_mutex_unlock(&s_elev_mu);
}

void fl_elevation_revoke_all(void) {
    pthread_mutex_lock(&s_elev_mu);
    mem_domain_zero(s_slots, sizeof(s_slots));
    pthread_mutex_unlock(&s_elev_mu);
}

size_t fl_elevation_active_count(void) {
    int64_t now = 0;
    size_t n = 0;

    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return 0;

    pthread_mutex_lock(&s_elev_mu);
#if defined(FL_STACK_ASM_AVAILABLE)
    n = asm_fl_elev_count_active(s_slots, FL_ELEV_MAX_SLOTS, now);
#else
    size_t i;
    for (i = 0; i < FL_ELEV_MAX_SLOTS; i++) {
        if (!s_slots[i].used)
            continue;
        if (s_slots[i].expires_ns > now)
            n++;
        else
            mem_domain_zero(&s_slots[i], sizeof(s_slots[i]));
    }
#endif
    pthread_mutex_unlock(&s_elev_mu);
    return n;
}

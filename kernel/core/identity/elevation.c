#include "elevation.h"
#include "user_db.h"
#include "timekeeping.h"
#include "mem_domain.h"
#include "fl/fl_stack_asm.h"
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

#define FL_ELEV_MAX_SLOTS 16u

static fl_elev_slot_t s_slots[FL_ELEV_MAX_SLOTS];
static fl_elevation_token_t s_next = 1u;
static pthread_mutex_t s_elev_mu = PTHREAD_MUTEX_INITIALIZER;

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
            s_slots[i].token = s_next ? s_next++ : 1u;
            strncpy(s_slots[i].principal, principal, sizeof(s_slots[i].principal) - 1);
            s_slots[i].principal[sizeof(s_slots[i].principal) - 1] = '\0';
            s_slots[i].expires_ns = now + (int64_t)FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS * 1000000000LL;
            *out = s_slots[i].token;
            pthread_mutex_unlock(&s_elev_mu);
            return FL_RESULT_OK;
        }
    }
    pthread_mutex_unlock(&s_elev_mu);
    return FL_RESULT_BUSY;
}

int fl_elevation_active(fl_elevation_token_t token) {
    int64_t now = 0;
    size_t i;
    int ok = 0;

    if (token == FL_ELEVATION_TOKEN_NONE)
        return 0;
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return 0;

    pthread_mutex_lock(&s_elev_mu);
    for (i = 0; i < FL_ELEV_MAX_SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].token == token) {
            if (s_slots[i].expires_ns > now)
                ok = 1;
            else
                mem_domain_zero(&s_slots[i], sizeof(s_slots[i]));
            break;
        }
    }
    pthread_mutex_unlock(&s_elev_mu);
    return ok;
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

#include "net_wifi_twt.h"
#include "timekeeping.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#define FL_NET_WIFI_HOSTED_LAB 1
#define FL_NET_WIFI_TWT_SLEEP_CAP_US 30000000ull

static fl_net_wifi_twt_params_t s_active_twt[8];
static uint8_t s_active_mask;
static uint64_t s_next_wake_abs_us;

static uint64_t twt_now_us(void)
{
    int64_t ns = 0;

    if (fl_time_monotonic_ns(&ns) != FL_RESULT_OK || ns < 0)
        return 0u;
    return (uint64_t)ns / 1000u;
}

static uint64_t twt_add_us(uint64_t a, uint64_t b)
{
    if (b > (UINT64_MAX - a))
        return UINT64_MAX;
    return a + b;
}

static uint64_t twt_flow_next_sp_abs(const fl_net_wifi_twt_params_t *p, uint64_t now)
{
    uint64_t target;
    uint64_t interval;
    uint64_t duration;
    uint64_t start;
    uint64_t n;

    if (!p)
        return 0u;
    interval = p->wake_interval_us;
    duration = p->wake_duration_us;
    target = p->twt_target_us;
    if (interval == 0u)
        return 0u;
    if (target == 0u)
        return twt_add_us(now, interval);
    if (now < target)
        return target;
    n = (now - target) / interval;
    start = twt_add_us(target, n * interval);
    if (duration == 0u)
        duration = 1u;
    if (now < twt_add_us(start, duration))
        return 0u; /* already in this service period — wake now */
    return twt_add_us(start, interval);
}

static void twt_recompute_next_wake(void)
{
    uint8_t id;
    uint64_t now = twt_now_us();
    uint64_t soonest = 0u;
    int have = 0;

    for (id = 0; id < 8u; id++) {
        uint64_t next;

        if ((s_active_mask & (1u << id)) == 0u)
            continue;
        next = twt_flow_next_sp_abs(&s_active_twt[id], now);
        if (next == 0u)
            continue;
        if (!have || next < soonest) {
            soonest = next;
            have = 1;
        }
    }
    s_next_wake_abs_us = have ? soonest : 0u;
}

fl_result_t fl_net_wifi_twt_negotiate(const fl_net_wifi_twt_params_t *req,
                                      fl_net_wifi_twt_params_t *agreed_out) {
#if !defined(FL_NET_WIFI_HOSTED_LAB)
    (void)req;
    (void)agreed_out;
    return FL_RESULT_NOSYS;
#else
    uint8_t id;

    if (!req || !agreed_out)
        return FL_RESULT_INVAL;
    if (req->wake_duration_us == 0u || req->wake_interval_us == 0u)
        return FL_RESULT_INVAL;

    for (id = 0; id < 8u; id++) {
        if ((s_active_mask & (1u << id)) == 0u)
            break;
    }
    if (id >= 8u)
        return FL_RESULT_ERR;

    memset(agreed_out, 0, sizeof(*agreed_out));
    *agreed_out = *req;
    agreed_out->flow_id = id;
    if (agreed_out->wake_interval_us < agreed_out->wake_duration_us)
        agreed_out->wake_interval_us = agreed_out->wake_duration_us * 2u;
    if (agreed_out->twt_target_us == 0u)
        agreed_out->twt_target_us = twt_add_us(twt_now_us(), agreed_out->wake_interval_us);
    s_active_twt[id] = *agreed_out;
    s_active_mask |= (1u << id);
    fl_net_wifi_twt_power_schedule(agreed_out);
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_twt_lab_teardown(uint8_t flow_id) {
#if !defined(FL_NET_WIFI_HOSTED_LAB)
    (void)flow_id;
    return FL_RESULT_NOSYS;
#else
    if (flow_id >= 8u)
        return FL_RESULT_INVAL;
    if ((s_active_mask & (1u << flow_id)) == 0u)
        return FL_RESULT_NOENT;
    memset(&s_active_twt[flow_id], 0, sizeof(s_active_twt[flow_id]));
    s_active_mask &= (uint8_t)~(1u << flow_id);
    twt_recompute_next_wake();
    return FL_RESULT_OK;
#endif
}

void fl_net_wifi_twt_lab_reset(void) {
    memset(s_active_twt, 0, sizeof(s_active_twt));
    s_active_mask = 0u;
    s_next_wake_abs_us = 0u;
}

uint64_t fl_net_wifi_twt_next_wake_us(void) {
    uint64_t now;

    twt_recompute_next_wake();
    if (s_next_wake_abs_us == 0u)
        return 0u;
    now = twt_now_us();
    if (now >= s_next_wake_abs_us)
        return 0u;
    return s_next_wake_abs_us - now;
}

int fl_net_wifi_twt_should_sleep(void) {
    return fl_net_wifi_twt_next_wake_us() > 0u;
}

void fl_net_wifi_twt_power_schedule(const fl_net_wifi_twt_params_t *agreed) {
    uint8_t id;

    if (!agreed || agreed->wake_interval_us == 0u)
        return;
    id = agreed->flow_id;
    if (id >= 8u)
        return;
    s_active_twt[id] = *agreed;
    if (s_active_twt[id].wake_duration_us == 0u)
        s_active_twt[id].wake_duration_us = 1u;
    if (s_active_twt[id].twt_target_us == 0u)
        s_active_twt[id].twt_target_us =
            twt_add_us(twt_now_us(), s_active_twt[id].wake_interval_us);
    s_active_mask |= (1u << id);
    twt_recompute_next_wake();
}

fl_result_t fl_net_wifi_twt_power_sleep(void) {
    uint64_t rem;
    struct timespec ts;

    rem = fl_net_wifi_twt_next_wake_us();
    if (rem == 0u)
        return FL_RESULT_OK;
    if (rem > FL_NET_WIFI_TWT_SLEEP_CAP_US)
        rem = FL_NET_WIFI_TWT_SLEEP_CAP_US;
    ts.tv_sec = (time_t)(rem / 1000000ull);
    ts.tv_nsec = (long)((rem % 1000000ull) * 1000ull);
    if (nanosleep(&ts, NULL) != 0)
        return FL_RESULT_ERR;
    (void)fl_net_wifi_twt_next_wake_us();
    return FL_RESULT_OK;
}

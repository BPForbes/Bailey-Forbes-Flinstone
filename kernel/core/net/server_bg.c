/*
 * server_bg.c — background pthread loops for the P3-13 server.
 *
 * One handle drives EITHER an accept/poll loop on the server side OR a
 * receive/dispatch loop on the client side. The two roles never share a
 * handle so callers always know which stop_* routine to call. Loops
 * sleep for short windows between passes so the foreground shell stays
 * responsive; the choice of 10/20/2 ms is small enough that an
 * interactive user does not notice latency on a join or message.
 *
 * Off-host (no POSIX threads / no BSD sockets) the public API resolves
 * to FL_RESULT_NOSYS so a baremetal link still succeeds.
 */
#include "server_bg.h"

#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define FL_SERVER_BG_HOSTED 1
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#endif

struct fl_server_bg_s {
#if defined(FL_SERVER_BG_HOSTED)
    pthread_t thread;
    /* 1 = server loop owns this handle; 0 = client loop owns it. */
    int is_server;
    /* Atomic stop signal; regular `=`/`!` on atomic_int are sequentially
     * consistent under C11, no extra pthread barrier needed. */
    atomic_int stop;
    /* Loop targets (only one of these is set per handle). */
    fl_net_server_t *srv;
    fl_net_client_t *client;
    fl_server_bg_client_cb client_cb;
    void *client_data;
#else
    int unused_placeholder;
#endif
};

#if defined(FL_SERVER_BG_HOSTED)

/* Cooperative sleep between loop passes — non-blocking sockets need a
 * yield or they busy-spin a core when nothing is pending. */
static void bg_sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    nanosleep(&ts, NULL);
}

/* Server side: try one accept and one round of per-member drains per
 * pass. Both accept_pending and poll_members are non-blocking and
 * tolerate "nothing to do", so the loop body stays cheap. */
static void *server_loop(void *arg) {
    fl_server_bg_t *h = (fl_server_bg_t *)arg;
    if (!h || !h->srv)
        return NULL;
    while (!h->stop) {
        (void)fl_net_server_accept_pending(h->srv, NULL, 0u);
        (void)fl_net_server_poll_members(h->srv);
        bg_sleep_ms(10u);
    }
    return NULL;
}

/* Client side: drain up to 16 frames per pass (cap chosen so a burst
 * from the host cannot starve the sleep cycle), shorter sleep when
 * something was dispatched to ride the burst, longer when idle. Loop
 * exits when the client transitions to DISCONNECTED (peer closed) so
 * the foreground reaper can free the handle on next verb_join. */
static void *client_loop(void *arg) {
    fl_server_bg_t *h = (fl_server_bg_t *)arg;
    if (!h || !h->client)
        return NULL;
    while (!h->stop) {
        int n = fl_net_client_poll(h->client, h->client_cb, h->client_data, 16u);
        if (n < 0)
            break;
        bg_sleep_ms(n == 0 ? 20u : 2u);
        if (fl_net_client_state(h->client) == FL_NET_CLIENT_STATE_DISCONNECTED)
            break;
    }
    return NULL;
}

/* Caller owns srv (it is stack/heap-allocated in cmd_server.c); we only
 * carry the pointer. calloc zeroes stop to 0 so the loop runs until the
 * matching stop_server flips it. */
fl_result_t fl_server_bg_start_server(fl_net_server_t *srv, fl_server_bg_t **handle_out) {
    fl_server_bg_t *h;
    if (!srv || !handle_out)
        return FL_RESULT_INVAL;
    h = (fl_server_bg_t *)calloc(1, sizeof(*h));
    if (!h)
        return FL_RESULT_NOMEM;
    h->is_server = 1;
    h->srv = srv;
    if (pthread_create(&h->thread, NULL, server_loop, h) != 0) {
        free(h);
        return FL_RESULT_ERR;
    }
    *handle_out = h;
    return FL_RESULT_OK;
}

/* pthread_join also synchronises memory: any state the loop touched is
 * visible to the caller after this returns. Safe on an already-exited
 * thread, which is how the verb_join reaper cleans up CTRL_KILL cases. */
fl_result_t fl_server_bg_stop_server(fl_server_bg_t *handle) {
    if (!handle)
        return FL_RESULT_INVAL;
    handle->stop = 1;
    pthread_join(handle->thread, NULL);
    free(handle);
    return FL_RESULT_OK;
}

/* `cb` and `data` are invoked from the bg thread for every event the
 * client decodes; the shell uses this to render colour-coded output
 * via the fl_colors helpers (which themselves take the shell_io lock). */
fl_result_t fl_server_bg_start_client(fl_net_client_t *client,
                                      fl_server_bg_client_cb cb, void *data,
                                      fl_server_bg_t **handle_out) {
    fl_server_bg_t *h;
    if (!client || !handle_out)
        return FL_RESULT_INVAL;
    h = (fl_server_bg_t *)calloc(1, sizeof(*h));
    if (!h)
        return FL_RESULT_NOMEM;
    h->is_server = 0;
    h->client = client;
    h->client_cb = cb;
    h->client_data = data;
    if (pthread_create(&h->thread, NULL, client_loop, h) != 0) {
        free(h);
        return FL_RESULT_ERR;
    }
    *handle_out = h;
    return FL_RESULT_OK;
}

/* Same semantics as the server stop. */
fl_result_t fl_server_bg_stop_client(fl_server_bg_t *handle) {
    if (!handle)
        return FL_RESULT_INVAL;
    handle->stop = 1;
    pthread_join(handle->thread, NULL);
    free(handle);
    return FL_RESULT_OK;
}

#else /* !FL_SERVER_BG_HOSTED */

/* Baremetal / non-POSIX builds: keep the symbols so the rest of the
 * tree links, but every entry point reports NOSYS so callers can choose
 * between disabling the feature or porting it to the target's threading
 * primitives. */
fl_result_t fl_server_bg_start_server(fl_net_server_t *srv, fl_server_bg_t **handle_out) {
    (void)srv;
    (void)handle_out;
    return FL_RESULT_NOSYS;
}
fl_result_t fl_server_bg_stop_server(fl_server_bg_t *handle) {
    (void)handle;
    return FL_RESULT_NOSYS;
}
fl_result_t fl_server_bg_start_client(fl_net_client_t *client,
                                      fl_server_bg_client_cb cb, void *data,
                                      fl_server_bg_t **handle_out) {
    (void)client;
    (void)cb;
    (void)data;
    (void)handle_out;
    return FL_RESULT_NOSYS;
}
fl_result_t fl_server_bg_stop_client(fl_server_bg_t *handle) {
    (void)handle;
    return FL_RESULT_NOSYS;
}

#endif /* FL_SERVER_BG_HOSTED */

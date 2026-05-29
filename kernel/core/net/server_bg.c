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
    /* CodeRabbit item 5: formally atomic stop signal. The previous
     * `volatile int` worked in practice on x86 + glibc (the loop reload
     * was forced by `volatile` and cross-thread visibility was
     * incidentally provided by surrounding pthread side effects), but
     * C11 strictly requires either `_Atomic` access or an explicit
     * pthread barrier for cross-thread observability of a plain int.
     * `atomic_int` makes the store/load relationship sequentially
     * consistent on every supported toolchain without affecting the
     * existing loops or callers. */
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

static void bg_sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    nanosleep(&ts, NULL);
}

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

static void *client_loop(void *arg) {
    fl_server_bg_t *h = (fl_server_bg_t *)arg;
    if (!h || !h->client)
        return NULL;
    while (!h->stop) {
        int n = fl_net_client_poll(h->client, h->client_cb, h->client_data, 16u);
        if (n < 0)
            break;
        if (n == 0)
            bg_sleep_ms(20u);
        else
            bg_sleep_ms(2u);
        if (fl_net_client_state(h->client) == FL_NET_CLIENT_STATE_DISCONNECTED)
            break;
    }
    return NULL;
}

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

fl_result_t fl_server_bg_stop_server(fl_server_bg_t *handle) {
    if (!handle)
        return FL_RESULT_INVAL;
    handle->stop = 1;
    pthread_join(handle->thread, NULL);
    free(handle);
    return FL_RESULT_OK;
}

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

fl_result_t fl_server_bg_stop_client(fl_server_bg_t *handle) {
    if (!handle)
        return FL_RESULT_INVAL;
    handle->stop = 1;
    pthread_join(handle->thread, NULL);
    free(handle);
    return FL_RESULT_OK;
}

#else /* !FL_SERVER_BG_HOSTED */

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

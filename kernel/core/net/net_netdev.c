#include "net_netdev.h"

#include "contract_p0_ci.h"
#include "net_arp.h"
#include "net_loopback.h"
#include "net_route.h"
#include "net_tap.h"
#include "net_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static fl_net_netdev_authz_fn s_authz_fn;
static void *s_authz_ctx;

static fl_net_driver_t s_loopback_drv;
static fl_net_driver_t s_tap_drv;
static int s_tap_fd = -1;
static char s_tap_ifname[16];
static char s_tap_error[96];

void fl_net_netdev_init(void) {
    fl_net_wire_init();
    fl_net_arp_init();
    fl_net_route_init();
    fl_net_loopback_reset();

    memset(&s_loopback_drv, 0, sizeof(s_loopback_drv));
    s_loopback_drv.send = fl_net_loopback_driver_send;
    s_loopback_drv.recv = fl_net_loopback_driver_recv;
    s_loopback_drv.mtu = FL_NET_ETH_MTU_DEFAULT;
    s_loopback_drv.advertised_caps = 0;
    s_loopback_drv.impl = NULL;

    memset(&s_tap_drv, 0, sizeof(s_tap_drv));
    s_tap_drv.send = fl_net_tap_driver_send;
    s_tap_drv.recv = fl_net_tap_driver_recv;
    s_tap_drv.mtu = FL_NET_ETH_MTU_DEFAULT;
    s_tap_drv.advertised_caps = FL_NET_DRV_CAP_PROMISC;
    s_tap_drv.impl = (void *)(intptr_t)-1;

    s_tap_fd = -1;
    s_tap_ifname[0] = '\0';
    s_tap_error[0] = '\0';
}

void fl_net_netdev_set_authz_hook(fl_net_netdev_authz_fn fn, void *ctx) {
    s_authz_fn = fn;
    s_authz_ctx = ctx;
}

fl_result_t fl_net_netdev_authz_check(unsigned op) {
    if (!s_authz_fn)
        return FL_RESULT_OK;
    return (s_authz_fn(op, s_authz_ctx) == FL_AUTHZ_ALLOW) ? FL_RESULT_OK : FL_RESULT_ACCES;
}

fl_net_driver_t *fl_net_netdev_loopback(void) {
    return &s_loopback_drv;
}

fl_net_driver_t *fl_net_netdev_tap(void) {
    return (s_tap_fd >= 0) ? &s_tap_drv : NULL;
}

fl_result_t fl_net_netdev_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame) {
    fl_result_t rc;

    if (!drv || !drv->send)
        return FL_RESULT_INVAL;

    rc = fl_net_wire_check_tx(frame, drv->mtu);
    if (rc != FL_RESULT_OK)
        return rc;

    return drv->send(drv, frame);
}

static fl_result_t netdev_recv_once(fl_net_driver_t *drv, fl_net_frame_mut_t *out) {
    if (!drv || !drv->recv)
        return FL_RESULT_INVAL;
    return drv->recv(drv, out);
}

static double netdev_elapsed_ms(const struct timeval *t0, const struct timeval *t1) {
    return (double)(t1->tv_sec - t0->tv_sec) * 1000.0 +
           (double)(t1->tv_usec - t0->tv_usec) / 1000.0;
}

fl_result_t fl_net_netdev_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out,
                               unsigned timeout_ms) {
    struct timeval t0, t1;
    fl_result_t rc;
    int fd = -1;

    if (!drv || !out)
        return FL_RESULT_INVAL;

    if (drv == &s_tap_drv && s_tap_fd >= 0)
        fd = s_tap_fd;

    gettimeofday(&t0, NULL);

    for (;;) {
        if (fd >= 0) {
            fd_set rfds;
            struct timeval tv;
            double left;

            gettimeofday(&t1, NULL);
            left = (double)timeout_ms - netdev_elapsed_ms(&t0, &t1);
            if (left <= 0.0)
                return FL_RESULT_TIMEDOUT;

            tv.tv_sec = (time_t)((unsigned)left / 1000u);
            tv.tv_usec = (suseconds_t)(((unsigned)left % 1000u) * 1000u);
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
                return FL_RESULT_TIMEDOUT;
        }

        rc = netdev_recv_once(drv, out);
        if (rc == FL_RESULT_OK)
            return rc;
        if (rc != FL_RESULT_TIMEDOUT)
            return rc;

        gettimeofday(&t1, NULL);
        if (netdev_elapsed_ms(&t0, &t1) >= (double)timeout_ms)
            return FL_RESULT_TIMEDOUT;

        if (fd >= 0)
            return FL_RESULT_TIMEDOUT;

        usleep(1000);
    }
}

fl_result_t fl_net_netdev_tap_open(const char *ifname_hint) {
    const char *skip;
    fl_result_t rc;

    skip = getenv(FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME);
    if (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_TAP_VALUE) == 0) {
        snprintf(s_tap_error, sizeof(s_tap_error), "%s=%s",
                 FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME, skip);
        return FL_RESULT_NOSYS;
    }

    if (s_tap_fd >= 0)
        return FL_RESULT_OK;

    rc = fl_net_tap_open(ifname_hint, &s_tap_fd, s_tap_ifname);
    if (rc != FL_RESULT_OK) {
        if (rc == FL_RESULT_ACCES)
            snprintf(s_tap_error, sizeof(s_tap_error), "TAP open denied (netdev register)");
        else if (rc == FL_RESULT_NOSYS)
            snprintf(s_tap_error, sizeof(s_tap_error), "TAP not available on this platform");
        else
            snprintf(s_tap_error, sizeof(s_tap_error), "TAP open failed");
        return rc;
    }

    s_tap_drv.impl = (void *)(intptr_t)s_tap_fd;
    s_tap_error[0] = '\0';

#if defined(__linux__)
    {
        uint8_t tap_mac[6];
        if (fl_net_tap_hwaddr(s_tap_fd, s_tap_ifname, tap_mac) == FL_RESULT_OK)
            (void)fl_net_route_configure_tap(&s_tap_drv, tap_mac, s_tap_ifname);
    }
#endif

    return FL_RESULT_OK;
}

void fl_net_netdev_tap_close(void) {
    if (s_tap_fd >= 0) {
        fl_net_tap_close(s_tap_fd);
        s_tap_fd = -1;
        s_tap_drv.impl = (void *)(intptr_t)-1;
    }
}

int fl_net_netdev_tap_is_open(void) {
    return s_tap_fd >= 0;
}

const char *fl_net_netdev_tap_last_error(void) {
    return s_tap_error;
}

void fl_net_netdev_stats(fl_net_driver_t *drv, fl_net_netdev_stats_t *out) {
    if (!out)
        return;
    out->tx_frames = 0;
    out->rx_frames = 0;
    if (drv == &s_loopback_drv) {
        out->tx_frames = fl_net_loopback_stat_tx();
        out->rx_frames = fl_net_loopback_stat_rx();
    } else if (drv == &s_tap_drv && s_tap_fd >= 0) {
        out->tx_frames = fl_net_tap_stat_tx();
        out->rx_frames = fl_net_tap_stat_rx();
    }
}

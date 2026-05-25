#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "net_tap.h"

#include "contract_p0_ci.h"
#include "net_netdev.h"
#include "net_wire.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/if.h>
#include <linux/if_tun.h>
#endif

static uint64_t s_tap_tx;
static uint64_t s_tap_rx;

#if defined(__linux__)

fl_result_t fl_net_tap_open(const char *ifname_hint, int *out_fd, char ifname_out[16]) {
    struct ifreq ifr;
    int fd;

    if (!out_fd || !ifname_out)
        return FL_RESULT_INVAL;

    if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_REGISTER) != FL_RESULT_OK)
        return FL_RESULT_ACCES;

    fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return (errno == EACCES || errno == EPERM) ? FL_RESULT_ACCES : FL_RESULT_ERR;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = (short)(IFF_TAP | IFF_NO_PI);
    if (ifname_hint && ifname_hint[0])
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname_hint);
    else
        snprintf(ifr.ifr_name, IFNAMSIZ, "fl%d", 0);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        close(fd);
        return FL_RESULT_ERR;
    }

    snprintf(ifname_out, 16, "%s", ifr.ifr_name);
    *out_fd = fd;
    return FL_RESULT_OK;
}

void fl_net_tap_close(int fd) {
    if (fd >= 0)
        close(fd);
}

fl_result_t fl_net_tap_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame) {
    ssize_t n;
    int fd;

    if (!drv || !frame || !frame->data || frame->len == 0)
        return FL_RESULT_INVAL;

    if (fl_net_wire_check_tx(frame, drv->mtu) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
        return FL_RESULT_ACCES;

    fd = (int)(intptr_t)drv->impl;
    if (fd < 0)
        return FL_RESULT_ERR;

    n = write(fd, frame->data, frame->len);
    if (n < 0)
        return FL_RESULT_ERR;
    if ((size_t)n != frame->len)
        return FL_RESULT_ERR;

    s_tap_tx++;
    return FL_RESULT_OK;
}

fl_result_t fl_net_tap_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out) {
    int fd;
    ssize_t n;

    if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
        return FL_RESULT_ACCES;

    fd = (int)(intptr_t)drv->impl;
    if (fd < 0)
        return FL_RESULT_ERR;

    n = read(fd, out->data, out->cap);
    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? FL_RESULT_TIMEDOUT : FL_RESULT_ERR;
    if (n == 0)
        return FL_RESULT_TIMEDOUT;

    if (fl_net_wire_check_rx_fill(out, (size_t)n) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    s_tap_rx++;
    return FL_RESULT_OK;
}

fl_result_t fl_net_tap_hwaddr(int fd, const char *ifname, uint8_t mac_out[6]) {
    struct ifreq ifr;

    if (fd < 0 || !ifname || !mac_out)
        return FL_RESULT_INVAL;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0)
        return FL_RESULT_ERR;
    memcpy(mac_out, ifr.ifr_hwaddr.sa_data, 6);
    return FL_RESULT_OK;
}

#else

fl_result_t fl_net_tap_open(const char *ifname_hint, int *out_fd, char ifname_out[16]) {
    (void)ifname_hint;
    (void)out_fd;
    (void)ifname_out;
    return FL_RESULT_NOSYS;
}

void fl_net_tap_close(int fd) {
    (void)fd;
}

fl_result_t fl_net_tap_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame) {
    (void)drv;
    (void)frame;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_tap_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out) {
    (void)drv;
    (void)out;
    return FL_RESULT_NOSYS;
}

#endif

uint64_t fl_net_tap_stat_tx(void) {
    return s_tap_tx;
}

uint64_t fl_net_tap_stat_rx(void) {
    return s_tap_rx;
}

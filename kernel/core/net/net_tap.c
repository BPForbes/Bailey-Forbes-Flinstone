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
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/sockios.h>
#endif

static uint64_t s_tap_tx;
static uint64_t s_tap_rx;

#if defined(__linux__)

static int tap_errno_acces(int err) {
    return err == EACCES || err == EPERM;
}

/* Linux TAP write() returns EIO until IFF_UP (WSL and recent host kernels). */
static fl_result_t fl_net_tap_if_up(const char *ifname) {
    struct ifreq ifr;
    int sock;

    if (!ifname || !ifname[0])
        return FL_RESULT_INVAL;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return tap_errno_acces(errno) ? FL_RESULT_ACCES : FL_RESULT_ERR;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        int err = errno;
        close(sock);
        return tap_errno_acces(err) ? FL_RESULT_ACCES : FL_RESULT_ERR;
    }
    if (ifr.ifr_flags & IFF_UP) {
        close(sock);
        return FL_RESULT_OK;
    }
    ifr.ifr_flags = (short)(ifr.ifr_flags | IFF_UP);
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        int err = errno;
        close(sock);
        return tap_errno_acces(err) ? FL_RESULT_ACCES : FL_RESULT_ERR;
    }
    close(sock);
    return FL_RESULT_OK;
}

fl_result_t fl_net_tap_open(const char *ifname_hint, int *out_fd, char ifname_out[16]) {
    struct ifreq ifr;
    int fd;
    fl_result_t up_rc;

    if (!out_fd || !ifname_out)
        return FL_RESULT_INVAL;

    if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_REGISTER) != FL_RESULT_OK)
        return FL_RESULT_ACCES;

    fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return tap_errno_acces(errno) ? FL_RESULT_ACCES : FL_RESULT_ERR;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = (short)(IFF_TAP | IFF_NO_PI);
    if (ifname_hint && ifname_hint[0])
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname_hint);
    else
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s", FL_NET_TAP_IFNAME_PATTERN);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        int err = errno;
        close(fd);
        return tap_errno_acces(err) ? FL_RESULT_ACCES : FL_RESULT_ERR;
    }

    up_rc = fl_net_tap_if_up(ifr.ifr_name);
    if (up_rc != FL_RESULT_OK) {
        close(fd);
        return up_rc;
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
        return tap_errno_acces(errno) ? FL_RESULT_ACCES : FL_RESULT_ERR;
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
    int sock;

    (void)fd;

    if (!ifname || !mac_out)
        return FL_RESULT_INVAL;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return FL_RESULT_ERR;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
        close(sock);
        return FL_RESULT_ERR;
    }
    memcpy(mac_out, ifr.ifr_hwaddr.sa_data, 6);
    close(sock);
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

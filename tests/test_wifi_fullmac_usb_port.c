/*
 * test_wifi_fullmac_usb_port.c - a USB port name is one sysfs entry.
 *
 * FL_WIFI_FULLMAC_USB names a directory under /sys/bus/usb/devices. A name that climbs out of it
 * ("../../../../tmp/x") would make the driver read idVendor and idProduct from another directory,
 * so it must be refused before any read. The rule is tested on its own function, because a probe
 * test needs a /sys/bus/usb tree, which many machines (and CI runners) do not have.
 */
#include "wifi_fullmac_hw_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

int main(void)
{
    char climb[PATH_MAX];

    /* Plain sysfs entry names: the kinds of port the kernel lists. */
    ASSERT(wifi_fullmac_usb_port_name_ok("1-1") == 1);
    ASSERT(wifi_fullmac_usb_port_name_ok("2-1.4") == 1);
    ASSERT(wifi_fullmac_usb_port_name_ok("usb1") == 1);
    ASSERT(wifi_fullmac_usb_port_name_ok("1-1:1.0") == 1);

    /* Anything that leaves /sys/bus/usb/devices, or names no entry. */
    ASSERT(wifi_fullmac_usb_port_name_ok(NULL) == 0);
    ASSERT(wifi_fullmac_usb_port_name_ok("") == 0);
    ASSERT(wifi_fullmac_usb_port_name_ok(".") == 0);
    ASSERT(wifi_fullmac_usb_port_name_ok("..") == 0);
    ASSERT(wifi_fullmac_usb_port_name_ok("a/b") == 0);
    ASSERT(wifi_fullmac_usb_port_name_ok("/tmp") == 0);
    ASSERT(wifi_fullmac_usb_port_name_ok("../x") == 0);
    snprintf(climb, sizeof(climb), "../../../../%s", "tmp/x");
    ASSERT(wifi_fullmac_usb_port_name_ok(climb) == 0);

    /* The probe refuses them too, before it reads anything. */
    {
        wifi_fullmac_hw_ctx_t ctx;

        memset(&ctx, 0, sizeof(ctx));
        setenv("FL_WIFI_FULLMAC_USB", climb, 1);
        ASSERT(wifi_fullmac_usb_probe_ctx(&ctx) == -1);
    }
    printf("test_wifi_fullmac_usb_port: ok\n");
    return 0;
}

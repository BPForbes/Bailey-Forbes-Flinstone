#ifndef KERNEL_DRIVERS_WIFI_FULLMAC_HW_INTERNAL_H
#define KERNEL_DRIVERS_WIFI_FULLMAC_HW_INTERNAL_H

#include "wifi_fullmac.h"
#include "wifi_fullmac_chipset.h"
#include "wifi_coprocessor.h"

typedef struct wifi_fullmac_hw_ctx {
	wifi_fullmac_chipset_t match;
	wifi_fullmac_bus_type_t bus;
	union {
		struct {
			uint8_t bus;
			uint8_t dev;
			uint8_t fn;
			uint32_t bar0;
		} pcie;
		struct {
			char port[32];
			char kernel_ifname[16];
		} usb;
	} loc;
	uint8_t *fw;
	size_t fw_len;
	wifi_network_t scan_cache[32];
	uint16_t scan_count;
} wifi_fullmac_hw_ctx_t;

const wifi_fullmac_ops_t *wifi_fullmac_hw_stub_ops(void);

int wifi_fullmac_hw_attach_ctx(wifi_fullmac_hw_ctx_t *ctx_in, wifi_fullmac_t **out_dev);
void wifi_fullmac_hw_fill_probe_info(const wifi_fullmac_hw_ctx_t *ctx,
				     wifi_fullmac_probe_info_t *info);

int wifi_fullmac_pcie_probe_ctx(wifi_fullmac_hw_ctx_t *ctx);
int wifi_fullmac_usb_probe_ctx(wifi_fullmac_hw_ctx_t *ctx);

/** Resolve Linux netdev (e.g. wlan1) bound to a USB sysfs port. */
int wifi_fullmac_usb_resolve_net_iface(const char *port, char *ifname_out, size_t cap);

/**
 * If a known ax USB device is present in sysfs, write its port (e.g. "2-2") to
 * port_out and return 0. Does not attach or require FL_WIFI_FULLMAC*.
 */
int wifi_fullmac_usb_sysfs_hint(uint16_t vendor_id, uint16_t device_id,
				char *port_out, size_t port_cap);

#endif /* KERNEL_DRIVERS_WIFI_FULLMAC_HW_INTERNAL_H */

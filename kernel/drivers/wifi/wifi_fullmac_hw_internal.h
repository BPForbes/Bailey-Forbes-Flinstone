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

#endif /* KERNEL_DRIVERS_WIFI_FULLMAC_HW_INTERNAL_H */

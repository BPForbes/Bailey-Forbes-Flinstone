#ifndef KERNEL_DRIVERS_WIFI_FULLMAC_CHIPSET_H
#define KERNEL_DRIVERS_WIFI_FULLMAC_CHIPSET_H

#include <stddef.h>
#include <stdint.h>

#include "wifi_fullmac.h"

typedef struct {
	const char *name;
	uint16_t vendor_id;
	uint16_t device_id;
	wifi_fullmac_bus_type_t bus;
} wifi_fullmac_chipset_t;

/** Return chipset row for vid:did, or NULL if unknown. */
const wifi_fullmac_chipset_t *wifi_fullmac_chipset_match(uint16_t vendor_id,
							 uint16_t device_id);

/** Enumerate built-in 802.11ax PCI IDs (table ends with vendor_id==0). */
const wifi_fullmac_chipset_t *wifi_fullmac_chipset_table(size_t *count_out);

#endif /* KERNEL_DRIVERS_WIFI_FULLMAC_CHIPSET_H */

#include "wifi_fullmac_chipset.h"

static const wifi_fullmac_chipset_t s_ax_chipsets[] = {
	{ "Intel AX200",         0x8086u, 0x2723u, WIFI_FULLMAC_BUS_PCIE },
	{ "Intel AX210",         0x8086u, 0x2725u, WIFI_FULLMAC_BUS_PCIE },
	{ "Intel AX211",         0x8086u, 0x51f0u, WIFI_FULLMAC_BUS_PCIE },
	{ "Qualcomm QCA6390",    0x17cbu, 0x1101u, WIFI_FULLMAC_BUS_PCIE },
	{ "Qualcomm WCN6855",    0x17cbu, 0x1103u, WIFI_FULLMAC_BUS_PCIE },
	{ "MediaTek MT7921",     0x14c3u, 0x7961u, WIFI_FULLMAC_BUS_PCIE },
	{ "MediaTek MT7922",     0x14c3u, 0x0616u, WIFI_FULLMAC_BUS_PCIE },
	{ "Realtek RTL8852AE",   0x10ecu, 0x8852u, WIFI_FULLMAC_BUS_PCIE },
	{ "Realtek RTL8852BE",   0x10ecu, 0xb852u, WIFI_FULLMAC_BUS_PCIE },
	{ "Broadcom BCM4377",    0x14e4u, 0x4499u, WIFI_FULLMAC_BUS_PCIE },
	{ NULL,                  0u,      0u,      WIFI_FULLMAC_BUS_PCIE },
};

const wifi_fullmac_chipset_t *wifi_fullmac_chipset_match(uint16_t vendor_id,
							 uint16_t device_id)
{
	size_t i;

	for (i = 0; s_ax_chipsets[i].name; i++) {
		if (s_ax_chipsets[i].vendor_id == vendor_id &&
		    s_ax_chipsets[i].device_id == device_id)
			return &s_ax_chipsets[i];
	}
	return NULL;
}

const wifi_fullmac_chipset_t *wifi_fullmac_chipset_table(size_t *count_out)
{
	size_t n = 0;

	while (s_ax_chipsets[n].name)
		n++;
	if (count_out)
		*count_out = n;
	return s_ax_chipsets;
}

/*
 * FullMAC WiFi 6 (802.11ax) Driver
 * Phase 4 of v4.3.0 WiFi drivers roadmap
 * Supports real PCIe/SDIO WiFi 6 hardware (Broadcom BCM6437xx, Qualcomm QCA639x)
 * Unblocks issue #279 production requirements and HE (High Efficiency) features
 */

#ifndef KERNEL_DRIVERS_WIFI_FULLMAC_H
#define KERNEL_DRIVERS_WIFI_FULLMAC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "wifi_coprocessor.h"
#include "contract_p3_wifi.h"

/* WiFi 6 (802.11ax) specific features */
#define WIFI_FULLMAC_HE_CAP_SUPPORT    1
#define WIFI_FULLMAC_OFDMA_SUPPORT    1
#define WIFI_FULLMAC_TWT_SUPPORT      1
#define WIFI_FULLMAC_MULTI_USER_SUPPORT 1

typedef enum {
	WIFI_FULLMAC_BUS_PCIE,
	WIFI_FULLMAC_BUS_SDIO,
	WIFI_FULLMAC_BUS_USB
} wifi_fullmac_bus_type_t;

typedef enum {
	WIFI_FULLMAC_STATE_DOWN,
	WIFI_FULLMAC_STATE_INITIALIZING,
	WIFI_FULLMAC_STATE_FW_LOADING,
	WIFI_FULLMAC_STATE_FW_READY,
	WIFI_FULLMAC_STATE_IDLE,
	WIFI_FULLMAC_STATE_SCANNING,
	WIFI_FULLMAC_STATE_CONNECTED,
	WIFI_FULLMAC_STATE_ERROR
} wifi_fullmac_state_t;

/* DMA Descriptor Ring for packet I/O */
#define WIFI_FULLMAC_RING_SIZE 256
#define WIFI_FULLMAC_PKT_MAX   2048

typedef struct {
	uint64_t dma_addr;  /* Physical address */
	uint32_t length;
	uint32_t flags;
	uint32_t reserved;
} wifi_fullmac_desc_t;

typedef struct {
	wifi_fullmac_desc_t desc[WIFI_FULLMAC_RING_SIZE];
	uint16_t head;
	uint16_t tail;
} wifi_fullmac_ring_t;

/* Command/Event Queue */
#define WIFI_FULLMAC_CMD_QUEUE_SIZE 32

typedef enum {
	WIFI_FULLMAC_CMD_SCAN,
	WIFI_FULLMAC_CMD_AUTH,
	WIFI_FULLMAC_CMD_ASSOC,
	WIFI_FULLMAC_CMD_DEAUTH,
	WIFI_FULLMAC_CMD_DISASSOC,
	WIFI_FULLMAC_CMD_SET_KEY,
	WIFI_FULLMAC_CMD_DEL_KEY,
	WIFI_FULLMAC_CMD_SET_TWT
} wifi_fullmac_cmd_type_t;

typedef struct {
	wifi_fullmac_cmd_type_t type;
	uint32_t seq;
	uint8_t data[256];
	size_t data_len;
	uint32_t timeout_ms;
} wifi_fullmac_cmd_t;

/* HE (High Efficiency) Capabilities */
typedef struct {
	uint32_t mac_cap_info;
	uint32_t phy_cap_info_1;
	uint32_t phy_cap_info_2;
	uint8_t mcs_nss[8]; /* MCS/NSS support matrix */
	bool ofdma_ul_supported;
	bool ofdma_dl_supported;
	uint8_t max_ampdu_len_exp;
	uint8_t default_pe_dur;
} wifi_fullmac_he_cap_t;

/* Target Wake Time (TWT) */
typedef struct {
	uint8_t flow_id;
	uint32_t wake_interval_ms;
	uint32_t wake_duration_us;
	bool requestor;
	bool trigger;
} wifi_fullmac_twt_setup_t;

/* FullMAC device structure */
typedef struct wifi_fullmac wifi_fullmac_t;

typedef struct {
	/* Lifecycle */
	int (*init)(wifi_fullmac_t *dev);
	int (*deinit)(wifi_fullmac_t *dev);
	int (*reset)(wifi_fullmac_t *dev);

	/* Firmware management */
	int (*load_firmware)(wifi_fullmac_t *dev, const uint8_t *fw, size_t fw_len);

	/* Command interface */
	int (*send_command)(wifi_fullmac_t *dev, const wifi_fullmac_cmd_t *cmd);
	int (*poll_event)(wifi_fullmac_t *dev, uint8_t *event_buf, size_t buf_len,
			  size_t *out_len);

	/* Scanning */
	int (*start_scan)(wifi_fullmac_t *dev, const char *ssid);
	int (*get_scan_results)(wifi_fullmac_t *dev, wifi_network_t *networks,
			       uint16_t *count);

	/* Association */
	int (*authenticate)(wifi_fullmac_t *dev, const uint8_t *bssid, uint16_t auth_type,
			   uint16_t auth_seq);
	int (*associate)(wifi_fullmac_t *dev, const uint8_t *bssid);
	int (*deauthenticate)(wifi_fullmac_t *dev, uint16_t reason);

	/* Key management */
	int (*set_key)(wifi_fullmac_t *dev, uint8_t key_index, const uint8_t *key,
		       size_t key_len);
	int (*delete_key)(wifi_fullmac_t *dev, uint8_t key_index);

	/* HE capabilities */
	int (*get_he_capabilities)(wifi_fullmac_t *dev, wifi_fullmac_he_cap_t *he_cap);
	int (*set_he_capabilities)(wifi_fullmac_t *dev,
				   const wifi_fullmac_he_cap_t *he_cap);

	/* TWT (Target Wake Time) */
	int (*setup_twt)(wifi_fullmac_t *dev, const wifi_fullmac_twt_setup_t *twt);
	int (*teardown_twt)(wifi_fullmac_t *dev, uint8_t flow_id);

	/* Packet I/O */
	int (*tx_packet)(wifi_fullmac_t *dev, const uint8_t *data, size_t len);
	int (*rx_packet)(wifi_fullmac_t *dev, uint8_t *buffer, size_t buf_len,
			 size_t *out_len);
} wifi_fullmac_ops_t;

struct wifi_fullmac {
	char name[16];
	wifi_fullmac_bus_type_t bus_type;
	wifi_fullmac_state_t state;

	/* Hardware interface */
	uint64_t bar0;	     /* BAR0 address (PCIe) */
	uint8_t irq;	     /* Interrupt line */
	uint16_t vendor_id; /* PCIe vendor ID */
	uint16_t device_id; /* PCIe device ID */

	/* DMA rings */
	wifi_fullmac_ring_t tx_ring;
	wifi_fullmac_ring_t rx_ring;

	/* Command queue */
	wifi_fullmac_cmd_t cmd_queue[WIFI_FULLMAC_CMD_QUEUE_SIZE];
	uint16_t cmd_head;
	uint16_t cmd_tail;

	/* HE capabilities */
	wifi_fullmac_he_cap_t he_cap;

	/* Backing netdev */
	fl_net_driver_t *netdev;

	/* Operations */
	const wifi_fullmac_ops_t *ops;
	void *driver_data;

	/* Statistics */
	uint64_t frames_tx;
	uint64_t frames_rx;
	uint64_t errors;
};

/* Public API */
int wifi_fullmac_pcie_create(uint16_t vendor_id, uint16_t device_id,
			     wifi_fullmac_t **out_dev);
int wifi_fullmac_pcie_destroy(wifi_fullmac_t *dev);

int wifi_fullmac_init(wifi_fullmac_t *dev);
int wifi_fullmac_deinit(wifi_fullmac_t *dev);

/* 802.11ax (WiFi 6) specific */
int wifi_fullmac_get_he_capabilities(wifi_fullmac_t *dev,
				     wifi_fullmac_he_cap_t *he_cap);
int wifi_fullmac_setup_twt(wifi_fullmac_t *dev, const wifi_fullmac_twt_setup_t *twt);

/** Identity from the last successful hardware probe (PCIe or USB). */
typedef struct {
	wifi_fullmac_bus_type_t bus;
	uint16_t vendor_id;
	uint16_t device_id;
	char chipset[48];
	char bdf[16];      /* PCIe BDF when bus is PCIE */
	char usb_port[32]; /* sysfs port e.g. 1-2 when bus is USB */
	uint32_t bar0;     /* PCIe BAR0; 0 for USB */
	uint8_t pci_bus;
	uint8_t pci_dev;
	uint8_t pci_fn;
} wifi_fullmac_probe_info_t;

/**
 * Attach a real FullMAC device (PCIe or USB). Requires one of:
 *   FL_WIFI_FULLMAC_PCI=0000:bb:dd.f
 *   FL_WIFI_FULLMAC_USB=bus-port (e.g. 1-2)
 *   FL_WIFI_FULLMAC_VIDPID=0e8d:7961
 *   FL_WIFI_FULLMAC=1 or FL_WIFI_FULLMAC_AUTO=1 (scan PCI then USB sysfs)
 * Optional: FL_WIFI_FULLMAC_FW=/path/to/firmware
 * Returns 0 on probe success (scan/connect may still need chipset firmware bring-up).
 */
int wifi_fullmac_hw_probe(wifi_fullmac_t **out_dev, wifi_fullmac_probe_info_t *info_out);
void wifi_fullmac_hw_detach(wifi_fullmac_t *dev);

/** Last probe/init error detail for shell/tests. */
const char *wifi_fullmac_last_error(void);

/**
 * If a USB ax dongle is visible in sysfs, write its port (e.g. "2-2") and return 0.
 * Does not require FL_WIFI_FULLMAC* or attach hardware.
 */
int wifi_fullmac_usb_sysfs_hint(uint16_t vendor_id, uint16_t device_id,
				char *port_out, size_t port_cap);

/** Driver-internal error buffer (wifi_fullmac_core.c). */
void wifi_fullmac_set_error(const char *msg);

/** Station connect via in-tree supplicant + FullMAC ops (Phase 4). */
int wifi_fullmac_station_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred);

#endif /* KERNEL_DRIVERS_WIFI_FULLMAC_H */

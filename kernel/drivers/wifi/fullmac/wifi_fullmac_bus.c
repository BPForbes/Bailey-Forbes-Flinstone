/*
 * FullMAC bus probe: PCIe BAR decode + USB dongle sysfs.
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_chipset.h"
#include "wifi_fullmac_hw_internal.h"

#include "fl/driver/pci.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#endif

static int pcie_parse_bdf(const char *s, uint8_t *bus, uint8_t *dev, uint8_t *fn)
{
	unsigned dom = 0, b = 0, d = 0, f = 0;

	if (!s || !bus || !dev || !fn)
		return -1;
	if (sscanf(s, "%x:%x:%x.%x", &dom, &b, &d, &f) == 4) {
		(void)dom;
	} else if (sscanf(s, "%x:%x.%x", &b, &d, &f) != 3)
		return -1;
	if (b > 255u || d > 31u || f > 7u)
		return -1;
	*bus = (uint8_t)b;
	*dev = (uint8_t)d;
	*fn = (uint8_t)f;
	return 0;
}

static uint32_t pcie_decode_bar0(uint32_t bar0_raw)
{
	if (bar0_raw == 0u || bar0_raw == 0xffffffffu)
		return 0u;
	if ((bar0_raw & 0x1u) == 0u)
		return bar0_raw & ~0xFu;
	return 0u;
}

static int pcie_read_ids(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t *vid, uint16_t *did)
{
	uint32_t id;

	if (!vid || !did)
		return -1;
	id = pci_read_config32(bus, dev, fn, 0u);
	if (id == 0xffffffffu || id == 0u)
		return -1;
	*vid = (uint16_t)(id & 0xffffu);
	*did = (uint16_t)((id >> 16) & 0xffffu);
	return 0;
}

static int pcie_apply_chipset(uint16_t vid, uint16_t did, wifi_fullmac_hw_ctx_t *ctx)
{
	const wifi_fullmac_chipset_t *chip;

	chip = wifi_fullmac_chipset_match(vid, did);
	if (!chip || chip->bus != WIFI_FULLMAC_BUS_PCIE) {
		const char *force = getenv("FL_WIFI_FULLMAC_FORCE");
		if (!force || !force[0] || strcmp(force, "0") == 0) {
			char msg[160];
			snprintf(msg, sizeof(msg),
				 "PCI vid:did %04x:%04x not in ax PCIe table "
				 "(set FL_WIFI_FULLMAC_FORCE=1 to attach anyway)",
				 (unsigned)vid, (unsigned)did);
			wifi_fullmac_set_error(msg);
			return -1;
		}
		ctx->match.name = "unknown ax PCIe (forced)";
		ctx->match.vendor_id = vid;
		ctx->match.device_id = did;
		ctx->match.bus = WIFI_FULLMAC_BUS_PCIE;
	} else {
		ctx->match = *chip;
	}
	ctx->bus = WIFI_FULLMAC_BUS_PCIE;
	return 0;
}

static int pcie_ctx_from_bdf(uint8_t bus, uint8_t dev, uint8_t fn, wifi_fullmac_hw_ctx_t *ctx)
{
	uint16_t vid = 0, did = 0;

	if (!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	if (pcie_read_ids(bus, dev, fn, &vid, &did) != 0) {
		wifi_fullmac_set_error("PCI config read failed or empty device");
		return -1;
	}
	if (pcie_apply_chipset(vid, did, ctx) != 0)
		return -1;
	ctx->loc.pcie.bus = bus;
	ctx->loc.pcie.dev = dev;
	ctx->loc.pcie.fn = fn;
	ctx->loc.pcie.bar0 = pcie_decode_bar0(pci_read_config32(bus, dev, fn, 0x10u));
	return 0;
}

#if defined(__linux__)
static int pcie_scan_sysfs(wifi_fullmac_hw_ctx_t *ctx)
{
	DIR *d;
	struct dirent *de;
	const char *want = getenv("FL_WIFI_FULLMAC_VIDPID");
	uint16_t want_vid = 0, want_did = 0;

	if (!ctx)
		return -1;
	if (want && want[0]) {
		unsigned v = 0, id = 0;
		if (sscanf(want, "%x:%x", &v, &id) == 2) {
			want_vid = (uint16_t)v;
			want_did = (uint16_t)id;
		}
	}
	d = opendir("/sys/bus/pci/devices");
	if (!d) {
		wifi_fullmac_set_error("cannot open /sys/bus/pci/devices");
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		uint8_t bus, dev_fn, fn;
		uint16_t vid = 0, did = 0;

		if (de->d_name[0] == '.')
			continue;
		if (pcie_parse_bdf(de->d_name, &bus, &dev_fn, &fn) != 0)
			continue;
		if (pcie_read_ids(bus, dev_fn, fn, &vid, &did) != 0)
			continue;
		if (want_vid && (vid != want_vid || did != want_did))
			continue;
		memset(ctx, 0, sizeof(*ctx));
		if (pcie_apply_chipset(vid, did, ctx) != 0)
			continue;
		ctx->loc.pcie.bus = bus;
		ctx->loc.pcie.dev = dev_fn;
		ctx->loc.pcie.fn = fn;
		ctx->loc.pcie.bar0 =
			pcie_decode_bar0(pci_read_config32(bus, dev_fn, fn, 0x10u));
		closedir(d);
		return 0;
	}
	closedir(d);
	wifi_fullmac_set_error("no known 802.11ax PCI device found");
	return -1;
}
#endif

#if !defined(__linux__)
static int pcie_scan_sysfs(wifi_fullmac_hw_ctx_t *ctx)
{
	(void)ctx;
	wifi_fullmac_set_error("PCI auto-scan requires Linux sysfs");
	return -1;
}
#endif

int wifi_fullmac_pcie_probe_ctx(wifi_fullmac_hw_ctx_t *ctx)
{
	const char *bdf_env;

	if (!ctx)
		return -1;
	bdf_env = getenv("FL_WIFI_FULLMAC_PCI");
	if (bdf_env && bdf_env[0]) {
		uint8_t bus, dev, fn;
		if (pcie_parse_bdf(bdf_env, &bus, &dev, &fn) != 0 ||
		    pcie_ctx_from_bdf(bus, dev, fn, ctx) != 0)
			return -1;
		return 0;
	}

	{
		const char *want = getenv("FL_WIFI_FULLMAC_VIDPID");
		if (want && want[0]) {
			unsigned v = 0, id = 0;
			if (sscanf(want, "%x:%x", &v, &id) == 2) {
				const wifi_fullmac_chipset_t *chip =
					wifi_fullmac_chipset_match((uint16_t)v, (uint16_t)id);
				if (chip && chip->bus != WIFI_FULLMAC_BUS_PCIE) {
					wifi_fullmac_set_error(
						"FL_WIFI_FULLMAC_VIDPID matches a USB chipset; "
						"use FL_WIFI_FULLMAC_USB=bus-port or auto USB scan");
					return -1;
				}
			}
		}
	}
	return pcie_scan_sysfs(ctx);
}

/* --- USB --- */

#include "wifi_fullmac_hw_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#endif

static int usb_sysfs_read_hex16(const char *syspath, const char *file, uint16_t *out)
{
	char path[512];
	FILE *fp;
	unsigned v = 0;

	if (!syspath || !file || !out)
		return -1;
	snprintf(path, sizeof(path), "%s/%s", syspath, file);
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	if (fscanf(fp, "%x", &v) != 1) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	*out = (uint16_t)v;
	return 0;
}

static int usb_apply_chipset(uint16_t vid, uint16_t did, wifi_fullmac_hw_ctx_t *ctx)
{
	const wifi_fullmac_chipset_t *chip;

	chip = wifi_fullmac_chipset_match(vid, did);
	if (!chip || chip->bus != WIFI_FULLMAC_BUS_USB) {
		const char *force = getenv("FL_WIFI_FULLMAC_FORCE");
		if (!force || !force[0] || strcmp(force, "0") == 0) {
			char msg[160];
			snprintf(msg, sizeof(msg),
				 "USB vid:pid %04x:%04x not in ax USB table "
				 "(set FL_WIFI_FULLMAC_FORCE=1 to attach anyway)",
				 (unsigned)vid, (unsigned)did);
			wifi_fullmac_set_error(msg);
			return -1;
		}
		ctx->match.name = "unknown ax USB (forced)";
		ctx->match.vendor_id = vid;
		ctx->match.device_id = did;
		ctx->match.bus = WIFI_FULLMAC_BUS_USB;
	} else {
		ctx->match = *chip;
	}
	ctx->bus = WIFI_FULLMAC_BUS_USB;
	return 0;
}

static int usb_ctx_from_port(const char *port, wifi_fullmac_hw_ctx_t *ctx)
{
	char syspath[512];
	uint16_t vid = 0, pid = 0;

	if (!port || !port[0] || !ctx)
		return -1;
	snprintf(syspath, sizeof(syspath), "/sys/bus/usb/devices/%s", port);
	if (usb_sysfs_read_hex16(syspath, "idVendor", &vid) != 0 ||
	    usb_sysfs_read_hex16(syspath, "idProduct", &pid) != 0) {
		char msg[128];
		snprintf(msg, sizeof(msg), "USB device %s not found in sysfs", port);
		wifi_fullmac_set_error(msg);
		return -1;
	}
	memset(ctx, 0, sizeof(*ctx));
	if (usb_apply_chipset(vid, pid, ctx) != 0)
		return -1;
	strncpy(ctx->loc.usb.port, port, sizeof(ctx->loc.usb.port) - 1u);
	(void)wifi_fullmac_usb_resolve_net_iface(port, ctx->loc.usb.kernel_ifname,
						 sizeof(ctx->loc.usb.kernel_ifname));
	return 0;
}

#if defined(__linux__)
static int usb_is_interface_name(const char *name)
{
	return strchr(name, ':') != NULL;
}

static int usb_scan_sysfs(wifi_fullmac_hw_ctx_t *ctx)
{
	DIR *d;
	struct dirent *de;
	const char *want = getenv("FL_WIFI_FULLMAC_VIDPID");
	uint16_t want_vid = 0, want_pid = 0;

	if (!ctx)
		return -1;
	if (want && want[0]) {
		unsigned v = 0, id = 0;
		if (sscanf(want, "%x:%x", &v, &id) == 2) {
			want_vid = (uint16_t)v;
			want_pid = (uint16_t)id;
		}
	}
	d = opendir("/sys/bus/usb/devices");
	if (!d) {
		wifi_fullmac_set_error("cannot open /sys/bus/usb/devices");
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		char syspath[512];
		uint16_t vid = 0, pid = 0;

		if (de->d_name[0] == '.')
			continue;
		if (usb_is_interface_name(de->d_name))
			continue;
		if (strlen(de->d_name) > 200u)
			continue;
		snprintf(syspath, sizeof(syspath), "/sys/bus/usb/devices/%s", de->d_name);
		if (usb_sysfs_read_hex16(syspath, "idVendor", &vid) != 0 ||
		    usb_sysfs_read_hex16(syspath, "idProduct", &pid) != 0)
			continue;
		if (want_vid && (vid != want_vid || pid != want_pid))
			continue;
		memset(ctx, 0, sizeof(*ctx));
		if (usb_apply_chipset(vid, pid, ctx) != 0)
			continue;
		strncpy(ctx->loc.usb.port, de->d_name, sizeof(ctx->loc.usb.port) - 1u);
		(void)wifi_fullmac_usb_resolve_net_iface(de->d_name, ctx->loc.usb.kernel_ifname,
							 sizeof(ctx->loc.usb.kernel_ifname));
		closedir(d);
		return 0;
	}
	closedir(d);
	wifi_fullmac_set_error("no known 802.11ax USB device found");
	return -1;
}
#endif

#if !defined(__linux__)
static int usb_scan_sysfs(wifi_fullmac_hw_ctx_t *ctx)
{
	(void)ctx;
	wifi_fullmac_set_error("USB auto-scan requires Linux sysfs");
	return -1;
}
#endif

int wifi_fullmac_usb_resolve_net_iface(const char *port, char *ifname_out, size_t cap)
{
#if defined(__linux__)
	char path[512];
	DIR *d;
	struct dirent *de;
	size_t port_len;

	if (!port || !port[0] || !ifname_out || cap == 0u)
		return -1;
	ifname_out[0] = '\0';
	snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/net", port);
	d = opendir(path);
	if (d) {
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.')
				continue;
			strncpy(ifname_out, de->d_name, cap - 1u);
			ifname_out[cap - 1u] = '\0';
			closedir(d);
			return 0;
		}
		closedir(d);
	}

	port_len = strlen(port);
	d = opendir("/sys/bus/usb/devices");
	if (!d)
		return -1;
	while ((de = readdir(d)) != NULL) {
		struct dirent *nde;

		if (de->d_name[0] == '.')
			continue;
		if (strncmp(de->d_name, port, port_len) != 0 || de->d_name[port_len] != ':')
			continue;
		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/net", de->d_name);
		{
			DIR *netd = opendir(path);
			if (!netd)
				continue;
			while ((nde = readdir(netd)) != NULL) {
				if (nde->d_name[0] == '.')
					continue;
				strncpy(ifname_out, nde->d_name, cap - 1u);
				ifname_out[cap - 1u] = '\0';
				closedir(netd);
				closedir(d);
				return 0;
			}
			closedir(netd);
		}
	}
	closedir(d);
#endif
	(void)port;
	(void)ifname_out;
	(void)cap;
	return -1;
}

int wifi_fullmac_usb_probe_ctx(wifi_fullmac_hw_ctx_t *ctx)
{
	const char *usb_env;

	if (!ctx)
		return -1;
	usb_env = getenv("FL_WIFI_FULLMAC_USB");
	if (usb_env && usb_env[0])
		return usb_ctx_from_port(usb_env, ctx);

	{
		const char *want = getenv("FL_WIFI_FULLMAC_VIDPID");
		if (want && want[0]) {
			unsigned v = 0, id = 0;
			if (sscanf(want, "%x:%x", &v, &id) == 2) {
				const wifi_fullmac_chipset_t *chip =
					wifi_fullmac_chipset_match((uint16_t)v, (uint16_t)id);
				if (chip && chip->bus != WIFI_FULLMAC_BUS_USB) {
					wifi_fullmac_set_error(
						"FL_WIFI_FULLMAC_VIDPID matches a PCIe chipset; "
						"use FL_WIFI_FULLMAC_PCI=b:d.f or auto PCI scan");
					return -1;
				}
			}
		}
	}
	return usb_scan_sysfs(ctx);
}

int wifi_fullmac_usb_sysfs_hint(uint16_t vendor_id, uint16_t device_id,
				char *port_out, size_t port_cap)
{
#if defined(__linux__)
	DIR *d;
	struct dirent *de;
	char syspath[512];

	if (!port_out || port_cap == 0)
		return -1;
	port_out[0] = '\0';
	d = opendir("/sys/bus/usb/devices");
	if (!d)
		return -1;
	while ((de = readdir(d)) != NULL) {
		uint16_t vid = 0, pid = 0;

		if (de->d_name[0] == '.')
			continue;
		if (usb_is_interface_name(de->d_name))
			continue;
		if (strlen(de->d_name) > 200u)
			continue;
		snprintf(syspath, sizeof(syspath), "/sys/bus/usb/devices/%s", de->d_name);
		if (usb_sysfs_read_hex16(syspath, "idVendor", &vid) != 0 ||
		    usb_sysfs_read_hex16(syspath, "idProduct", &pid) != 0)
			continue;
		if (vid != vendor_id || pid != device_id)
			continue;
		strncpy(port_out, de->d_name, port_cap - 1u);
		port_out[port_cap - 1u] = '\0';
		closedir(d);
		return 0;
	}
	closedir(d);
#endif
	(void)vendor_id;
	(void)device_id;
	(void)port_out;
	(void)port_cap;
	return -1;
}

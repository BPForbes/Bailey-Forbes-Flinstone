/*
 * Phase 4 USB FullMAC: sysfs USB device probe for dongles (e.g. MediaTek MT7921AU).
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_chipset.h"
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
	char syspath[256];
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
		char syspath[256];
		uint16_t vid = 0, pid = 0;

		if (de->d_name[0] == '.')
			continue;
		if (usb_is_interface_name(de->d_name))
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

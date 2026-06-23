#include "wifi_fullmac_fw.h"

#include "wifi_platform.h"

#include <stdio.h>
#include <stdlib.h>

int wifi_fullmac_fw_load_file(const char *path, uint8_t **out_buf, size_t *out_len)
{
	FILE *fp;
	long sz;
	uint8_t *buf;

	if (!path || !path[0] || !out_buf || !out_len)
		return -1;
	*out_buf = NULL;
	*out_len = 0;

	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return -1;
	}
	sz = ftell(fp);
	if (sz <= 0 || sz > (long)(32u * 1024u * 1024u)) {
		fclose(fp);
		return -1;
	}
	rewind(fp);
	buf = (uint8_t *)wifi_platform_malloc((size_t)sz);
	if (!buf) {
		fclose(fp);
		return -1;
	}
	if (fread(buf, 1u, (size_t)sz, fp) != (size_t)sz) {
		wifi_platform_free(buf);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	*out_buf = buf;
	*out_len = (size_t)sz;
	return 0;
}

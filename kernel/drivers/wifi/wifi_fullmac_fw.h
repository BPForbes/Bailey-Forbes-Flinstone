#ifndef KERNEL_DRIVERS_WIFI_FULLMAC_FW_H
#define KERNEL_DRIVERS_WIFI_FULLMAC_FW_H

#include <stddef.h>
#include <stdint.h>

/** Load firmware blob from path into heap buffer (*out_buf, caller frees via wifi_platform_free). */
int wifi_fullmac_fw_load_file(const char *path, uint8_t **out_buf, size_t *out_len);

#endif /* KERNEL_DRIVERS_WIFI_FULLMAC_FW_H */

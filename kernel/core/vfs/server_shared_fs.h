#ifndef SERVER_SHARED_FS_H
#define SERVER_SHARED_FS_H

#include "contract_p5_file_delivery.h"

#include <stddef.h>
#include <stdint.h>

/** Ensure ./server_shared and ./server_shared/expired exist. */
fl_result_t fl_server_shared_init(void);

/** True when resolved path lies under server_shared/expired (hard deny for all principals). */
int fl_server_shared_path_is_expired_quarantine(const char *path);

/** Compose server_shared landing basename: `<share_id>_<file_name>`. */
fl_result_t fl_server_shared_landed_basename(const fl_server_file_offer_t *offer,
                                             char *out,
                                             size_t out_cap);

/** Save bytes to server_shared/<share_id>_<file_name> (session meta is never persisted). */
fl_result_t fl_server_shared_save_offer(const fl_server_file_offer_t *offer,
                                        const uint8_t *data,
                                        size_t data_len,
                                        char *out_path,
                                        size_t out_path_cap);

/** Overwrite a local destination path when permitted. */
fl_result_t fl_server_shared_overwrite_local(const fl_server_file_offer_t *offer,
                                             const char *local_path,
                                             const uint8_t *data,
                                             size_t data_len);

/** No-op: in-flight expiry is handled in session memory, not on-disk sidecars. */
fl_result_t fl_server_shared_purge_expired(uint64_t now);

#endif /* SERVER_SHARED_FS_H */

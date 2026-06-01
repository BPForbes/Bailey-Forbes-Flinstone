#ifndef SERVER_SHARED_FS_H
#define SERVER_SHARED_FS_H

#include "contract_p5_file_delivery.h"

#include <stddef.h>
#include <stdint.h>

#define FL_SERVER_SHARED_LANDED_MAX \
    (FL_SERVER_SHARE_ID_MAX + FL_SERVER_FILE_NAME_MAX + 32u)

/** Ensure ./server_shared and ./server_shared/expired exist. */
fl_result_t fl_server_shared_init(void);

/** True when resolved path lies under server_shared/expired (hard deny for all principals). */
int fl_server_shared_path_is_expired_quarantine(const char *path);

/** Compose collision-free flat landing basename: `<sidlen>$<share_id>$<fnlen>$<file_name>`. */
fl_result_t fl_server_shared_landed_basename(const fl_server_file_offer_t *offer,
                                             char *out,
                                             size_t out_cap);

/** Save bytes to server_shared/<landed>; writes `<landed>.meta` when expires_at is set. */
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

/** Move expired on-disk server_shared payloads (and their .meta sidecars) into expired/. */
fl_result_t fl_server_shared_purge_expired(uint64_t now);

#endif /* SERVER_SHARED_FS_H */

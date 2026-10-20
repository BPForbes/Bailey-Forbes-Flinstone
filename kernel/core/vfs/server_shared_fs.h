#ifndef SERVER_SHARED_FS_H
#define SERVER_SHARED_FS_H

#include "contract_p5_file_delivery.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define FL_SERVER_SHARED_LANDED_MAX \
    (FL_SERVER_SHARE_ID_MAX + FL_SERVER_FILE_NAME_MAX + 32u)

/* ---- file helpers shared by the server share and KeyQuorum delivery ------- */

/** Longest path the helpers below accept, including the ".part" suffix they add. */
#define FL_FS_PATH_MAX 4096u

/**
 * mkdir -p: creates path and its missing parents with mode. 0 on success (also when it
 * exists); -1 with errno set otherwise, including ENAMETOOLONG for a path that does not
 * fit (it is never truncated).
 */
int fl_fs_mkdir_p(const char *path, mode_t mode);
/** Writes data to <path>.part, owner-only and synced; leaves nothing behind on failure. */
int fl_fs_stage_file(const char *path, const uint8_t *data, size_t len);
/** Replaces path atomically: stages <path>.part, then renames it over path. 0 on success. */
int fl_fs_write_file_atomic(const char *path, const uint8_t *data, size_t len);

/** Ensure ./server_shared and ./server_shared/expired exist. */
fl_result_t fl_server_shared_init(void);

/** True when resolved path lies under server_shared/expired (hard deny for all principals). */
int fl_server_shared_path_is_expired_quarantine(const char *path);

/** Preferred landing basename (`file_name`); save may disambiguate with ` (n)` suffix. */
fl_result_t fl_server_shared_landed_basename(const fl_server_file_offer_t *offer,
                                             char *out,
                                             size_t out_cap);

/** Save bytes to server_shared/<landed>; writes `<landed>.meta` (share_id, optional expires_at). */
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

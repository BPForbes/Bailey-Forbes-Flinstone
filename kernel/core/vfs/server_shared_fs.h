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
/**
 * Reads the whole regular file at path (binary; symlinks are followed). One open, size from
 * fstat, one allocation, then an EOF probe, so a file that grew or shrank is never returned
 * truncated. 0 on success: *data is heap memory (free() it; never NULL, an empty file gives
 * *len 0) and *len its size. On failure *data is NULL, *len 0, any bytes already read are
 * wiped, and the result is -1 (not a regular file (errno EINVAL; a directory, FIFO or device,
 * none is ever blocked on), a NULL path, or a stat, read, shrink or allocation failure),
 * -2 (larger than max, or grew while read; errno EFBIG) or -3 (could not be opened; errno from
 * open, so ENOENT for a missing file). Applies no path validation. On success the caller wipes
 * a secret buffer itself (fl_kq_buf_free where it holds an fl_kq_buf_t); this only wipes failures.
 */
int fl_fs_read_file(const char *path, size_t max, uint8_t **data, size_t *len);

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

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

/** Longest single name component the file systems in use take (NAME_MAX on Linux). */
#define FL_FS_NAME_MAX 255u
/** Suffix of the staging file a write goes through before it is renamed into place. */
#define FL_FS_PART_SUFFIX ".part"
/** Suffix of the backup fl_fs_commit_files keeps of a file it replaces (a reserved name). */
#define FL_FS_BACKUP_SUFFIX ".kqbak"

/**
 * mkdir -p: creates path and its missing parents with mode. 0 on success (also when it
 * exists); -1 with errno set otherwise, including ENAMETOOLONG for a path that does not
 * fit (it is never truncated).
 */
int fl_fs_mkdir_p(const char *path, mode_t mode);
/**
 * Writes data to <path>.part, owner-only and synced; leaves nothing behind on failure. The
 * .part file is created new and never through a link: a leftover at that name is removed
 * first, and a link there is removed, not written through.
 */
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

/*
 * Contained access: a directory is opened once and names are then opened relative to that
 * fd, refusing links. They need the *at calls (otherwise they fail with ENOSYS). The fd is
 * for callers of this file only and is not part of fs_provider_t.
 */

/** Opens dir for the calls below (its own path is trusted and may be a link). fd, or -1. */
int fl_fs_dir_open(const char *dir);
/**
 * fl_fs_read_file for one plain name inside dirfd: no separator, not "." or "..", and a link
 * at that name is refused (-3, errno ELOOP). Same results and ownership as fl_fs_read_file;
 * a refused name is -1 with errno EINVAL.
 */
int fl_fs_read_file_in(int dirfd, const char *name, size_t max, uint8_t **data, size_t *len);
/**
 * fl_fs_write_file_atomic for one plain name inside dirfd (same rules as fl_fs_read_file_in).
 * The new file never replaces anything but the file at name itself; 0 on success, else -1.
 */
int fl_fs_write_file_atomic_in(int dirfd, const char *name, const uint8_t *data, size_t len);

/**
 * Joins dir and name into out (cap bytes, always terminated on success): an empty dir gives
 * name alone and a trailing '/' on dir is not doubled. 0 on success; on a result that does not
 * fit it is -ENAMETOOLONG (errno set too); for a NULL out or cap 0 it is -1. out is then not
 * to be used.
 */
int fl_fs_join_path(char *out, size_t cap, const char *dir, const char *name);

/**
 * 1 when name ends in a suffix the delivery code keeps for its own files (.part, .meta, .kqtf,
 * .kqseen, .kqbak), compared ignoring ASCII case. The one list both landing paths use.
 */
int fl_server_shared_name_is_reserved(const char *name);
/** 1 when the contained calls above and below work on this platform (they need the *at calls). */
int fl_fs_contained_available(void);
/** fl_fs_stage_file for one plain name inside dirfd: writes <name>.part, never through a link. */
int fl_fs_stage_file_in(int dirfd, const char *name, const uint8_t *data, size_t len);
/** Renames one plain name to another inside dirfd (a link at the target is replaced, not followed). */
int fl_fs_rename_in(int dirfd, const char *from, const char *to);
/** Removes one plain name inside dirfd (a link is removed itself). 0 on success, else -1. */
int fl_fs_unlink_in(int dirfd, const char *name);
/** Hard-links from to to inside dirfd without following a link at from. 0 on success, else -1. */
int fl_fs_link_in(int dirfd, const char *from, const char *to);
/** 1 when something (a link included) exists at name inside dirfd, 0 when not, -1 on error. */
int fl_fs_exists_in(int dirfd, const char *name);
/** Opens one plain name inside dirfd for reading, refusing links (errno ELOOP). fd, or -1. */
int fl_fs_open_read_in(int dirfd, const char *name);

/*
 * A landing directory is either held as an fd (dirfd >= 0, contained calls: links are never
 * followed) or, only where the contained calls do not exist at all (fl_fs_contained_available()
 * is 0), named by dir_path and used through the path-based calls. With the contained calls
 * available, dirfd < 0 is refused (-1, errno EBADF): the path is never a silent fallback.
 */

/** fl_fs_exists_in for a landing directory as above. 1 present (a link counts), 0 absent, -1. */
int fl_fs_exists_at(int dirfd, const char *dir_path, const char *name);
/** fl_fs_read_file_in for a landing directory as above (same results and ownership). */
int fl_fs_read_file_at(int dirfd, const char *dir_path, const char *name, size_t max,
                       uint8_t **data, size_t *len);

/** One file for fl_fs_commit_files: a plain name inside the landing directory and its bytes. */
typedef struct {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} fl_fs_file_out_t;

/** Longest name fl_fs_commit_files takes; leaves room for ".kqbak.part" (a copied backup). */
#define FL_FS_COMMIT_NAME_MAX (FL_FS_NAME_MAX - 11u)
/** Most files one fl_fs_commit_files call takes. */
#define FL_FS_COMMIT_FILES_MAX 4u

/**
 * Replaces every file in files[0..n) or none of them, inside one landing directory (dirfd and
 * dir_path as above; the caller opens dirfd with fl_fs_dir_open and closes it, this never
 * does). All <name>.part files are staged first; then each existing file is kept as
 * <name>.kqbak (a hard link, or a copy where links are missing, wiped from memory after) and
 * the parts are renamed into place. If a step fails, the files already replaced are put
 * back from their backups (or removed when they did not exist), the parts are removed, and the
 * result is -1 with errno of the first failure; the backups are removed at the end.
 * A stale <name>.kqbak is dropped first. Names are plain (no separator, not "." or ".."), at
 * most FL_FS_COMMIT_NAME_MAX (244) bytes, the same with or without hard links, and must not
 * repeat or be another name plus ".part" or ".kqbak". n is 1 to FL_FS_COMMIT_FILES_MAX; any
 * other n, or a refused name, is -1 (EINVAL, or ENAMETOOLONG) before anything is touched.
 * Copy fallback: the file is read whole, up to 4 MiB (the KeyQuorum letter bound); a larger
 * existing file makes the replace fail (nothing changed) where links are missing.
 * An existence check that cannot tell fails the call before any file is replaced (-1, its
 * errno); the parts are removed.
 * Known limit (as the code this replaced): "put back" is best effort. If the restoring rename
 * itself fails, that file keeps its new bytes and its backup is still removed. A fix changes
 * the .kqbak lifecycle and needs its own design.
 * Not safe against another writer racing in the same directory. 0 on success.
 */
int fl_fs_commit_files(int dirfd, const char *dir_path, const fl_fs_file_out_t *files, size_t n);

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

/**
 * Resolves a relative path under the shell's working directory for writing. A lexical gate
 * (not empty, not absolute, no backslash, no ".." component, not under server_shared/expired),
 * then each directory component is opened by fd without following links (missing ones are
 * created owner-only when create_parents). On FL_RESULT_OK *dir_fd is the open parent
 * directory (the caller closes it) and leaf its last component. FL_RESULT_ACCES for a refused
 * path or a link on the way, FL_RESULT_INVAL for a bad or too long one.
 */
fl_result_t fl_server_shared_resolve_local_path(const char *local_path,
                                                int create_parents,
                                                int *dir_fd,
                                                char *leaf,
                                                size_t leaf_cap);

/**
 * Overwrite a local destination path when permitted (through the contained walk; refuses
 * links and non-regular files). The data is staged as a new owner-only (0600) file and renamed
 * into place, so a failed write keeps the old content and the result always has mode 0600.
 * An existing destination must also be writable by the caller (else FL_RESULT_ACCES). The
 * replacement needs write access to the directory, and the old file's owner, ACL and extended
 * attributes are not carried over. Staging removes any existing non-directory "<leaf>.part" in
 * the same directory, so a file with that name is not preserved.
 */
fl_result_t fl_server_shared_overwrite_local(const fl_server_file_offer_t *offer,
                                             const char *local_path,
                                             const uint8_t *data,
                                             size_t data_len);

/** Move expired on-disk server_shared payloads (and their .meta sidecars) into expired/. */
fl_result_t fl_server_shared_purge_expired(uint64_t now);

#endif /* SERVER_SHARED_FS_H */

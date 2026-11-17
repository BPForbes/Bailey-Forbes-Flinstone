#ifndef SERVER_SHARED_FS_H
#define SERVER_SHARED_FS_H

/* Placement layer: bytes are staged and renamed, never written through a link; new files 0600. */
#include "contract_p5_file_delivery.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define FL_SERVER_SHARED_LANDED_MAX \
    (FL_SERVER_SHARE_ID_MAX + FL_SERVER_FILE_NAME_MAX + 32u)


#define FL_FS_PATH_MAX 4096u

#define FL_FS_NAME_MAX 255u
#define FL_FS_PART_SUFFIX ".part"
#define FL_FS_BACKUP_SUFFIX ".kqbak"

int fl_fs_mkdir_p(const char *path, mode_t mode);
int fl_fs_stage_file(const char *path, const uint8_t *data, size_t len);
int fl_fs_write_file_atomic(const char *path, const uint8_t *data, size_t len);
int fl_fs_read_file(const char *path, size_t max, uint8_t **data, size_t *len);


int fl_fs_dir_open(const char *dir);
int fl_fs_dir_open_nofollow(const char *dir);
int fl_fs_trusted_root_open(const char *dir);
int fl_fs_read_file_in(int dirfd, const char *name, size_t max, uint8_t **data, size_t *len);
int fl_fs_write_file_atomic_in(int dirfd, const char *name, const uint8_t *data, size_t len);

int fl_fs_join_path(char *out, size_t cap, const char *dir, const char *name);

int fl_server_shared_name_is_reserved(const char *name);
int fl_fs_contained_available(void);
int fl_fs_stage_file_in(int dirfd, const char *name, const uint8_t *data, size_t len);
int fl_fs_rename_in(int dirfd, const char *from, const char *to);
int fl_fs_unlink_in(int dirfd, const char *name);
int fl_fs_link_in(int dirfd, const char *from, const char *to);
int fl_fs_exists_in(int dirfd, const char *name);
int fl_fs_open_read_in(int dirfd, const char *name);


int fl_fs_exists_at(int dirfd, const char *dir_path, const char *name);
int fl_fs_read_file_at(int dirfd, const char *dir_path, const char *name, size_t max,
                       uint8_t **data, size_t *len);
int fl_fs_read_tail_at(int dirfd, const char *dir_path, const char *name, size_t unit,
                       size_t keep, uint8_t **data, size_t *len);

typedef struct {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} fl_fs_file_out_t;

#define FL_FS_COMMIT_NAME_MAX (FL_FS_NAME_MAX - 11u)
#define FL_FS_COMMIT_FILES_MAX 4u

/* Replaces every file or none; a stale <name>.kqbak is dropped first. */
int fl_fs_commit_files(int dirfd, const char *dir_path, const fl_fs_file_out_t *files, size_t n);

fl_result_t fl_server_shared_init(void);

int fl_server_shared_path_is_expired_quarantine(const char *path);

fl_result_t fl_server_shared_landed_basename(const fl_server_file_offer_t *offer,
                                             char *out,
                                             size_t out_cap);

fl_result_t fl_server_shared_save_offer(const fl_server_file_offer_t *offer,
                                        const uint8_t *data,
                                        size_t data_len,
                                        char *out_path,
                                        size_t out_path_cap);

fl_result_t fl_server_shared_resolve_local_path(const char *local_path,
                                                int create_parents,
                                                int *dir_fd,
                                                char *leaf,
                                                size_t leaf_cap);

fl_result_t fl_server_shared_overwrite_local(const fl_server_file_offer_t *offer,
                                             const char *local_path,
                                             const uint8_t *data,
                                             size_t data_len);

fl_result_t fl_server_shared_purge_expired(uint64_t now);

#endif /* SERVER_SHARED_FS_H */

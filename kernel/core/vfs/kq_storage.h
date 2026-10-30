#ifndef FL_KQ_STORAGE_H
#define FL_KQ_STORAGE_H

/*
 * Where a KeyQuorum checkpoint (kq_checkpoint.h) reads and writes files. The checkpoints decide
 * what a delivery means and take every byte-moving step through this table, the way KeyQuorum's
 * logic modules take a Storage (src/storage.rs) and never touch the disk themselves:
 *
 *   default          the placement layer (server_shared_fs.c, fl_fs_*): staged writes,
 *                    no-follow reads, the dirfd-relative landing and fl_fs_commit_files. The
 *                    browser lab and the unit tests use it.
 *   facade           fm_* / fm_dir_* (kq_storage_facade.c), so the native shell's policy, events
 *                    and path log see every checkpoint read and write, over the provider's
 *                    contained slots (fs_provider.h), which are built on the same layer.
 *
 * Two kinds of access, because a delivery lands in a directory it must hold:
 *
 *   path-based       the sender's file and its container beside it (encode, history, head,
 *                    reseal, permit).
 *   held directory   the recipient's landing (decode): the directory is held as an fd and every
 *                    name is used inside it, so a link planted at a name is never followed,
 *                    and the payload, container and seen record replace together or not at all.
 *
 * A table never changes what a result means: codes follow fl_fs_read_file (0, -1, -2 larger than
 * max, -3 could not be opened), fl_fs_exists_at (1, 0, -1) and fl_fs_commit_files (0 or -1).
 * Data handed back is MEM_DOMAIN_FS memory (mem_domain_free, or fl_kq_buf_free in a buffer).
 */

#include <stddef.h>
#include <stdint.h>

/** A held landing directory: the fd (-1 only where there are no *at calls) and its path. */
typedef struct {
    int         fd;
    const char *dir;
} fl_kq_dir_t;

/** One file of dir_commit: a plain name inside the held directory and its bytes. */
typedef struct {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} fl_kq_file_out_t;

typedef struct fl_kq_storage fl_kq_storage_t;

struct fl_kq_storage {
    void *ctx;
    int (*read_file)(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len);
    int (*write_atomic)(void *ctx, const char *path, const uint8_t *data, size_t len);
    /** 1 present, 0 absent, -1 cannot tell (a permission error is not "absent"). */
    int (*exists)(void *ctx, const char *path);
    /** Holds dir; 0, or -1 when it cannot be held (refused, never a path fallback). */
    int (*dir_open)(void *ctx, const char *dir, fl_kq_dir_t *out);
    void (*dir_close)(void *ctx, fl_kq_dir_t *d);
    int (*dir_read)(void *ctx, const fl_kq_dir_t *d, const char *name, size_t max, uint8_t **data,
                    size_t *len);
    int (*dir_exists)(void *ctx, const fl_kq_dir_t *d, const char *name);
    int (*dir_read_tail)(void *ctx, const fl_kq_dir_t *d, const char *name, size_t unit,
                         size_t keep, uint8_t **data, size_t *len);
    /** All of files[0..n) replace or none; n is 1 to 4. */
    int (*dir_commit)(void *ctx, const fl_kq_dir_t *d, const fl_kq_file_out_t *files, size_t n);
};

/** The placement-layer table: the default, with no allocation. */
const fl_kq_storage_t *fl_kq_storage_default(void);

/**
 * The table every checkpoint uses from now on; NULL restores the default. The table must outlive
 * its use. Set it once at start (the native shell installs the facade table), not while a
 * checkpoint runs. Returns the table that was in force.
 */
const fl_kq_storage_t *fl_kq_cp_set_storage(const fl_kq_storage_t *storage);

/** The table in force (never NULL). */
const fl_kq_storage_t *fl_kq_cp_storage(void);

#endif /* FL_KQ_STORAGE_H */

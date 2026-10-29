/*
 * The KeyQuorum checkpoints' storage over the file manager facade (kq_storage_facade.h). It
 * decides nothing: each call is the facade's own, and the facade asks the policy, runs the
 * write chain and records the path log before and after the provider's contained slot.
 */
#include "kq_storage_facade.h"

#include "fs_provider.h"

#include <stddef.h>

static file_manager_service_t *svc_of(void *ctx) {
    return (file_manager_service_t *)ctx;
}

static int fac_read_file(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len) {
    return fm_read_bytes(svc_of(ctx), path, max, data, len);
}

static int fac_write_atomic(void *ctx, const char *path, const uint8_t *data, size_t len) {
    return fm_write_atomic(svc_of(ctx), path, data, len);
}

static int fac_exists(void *ctx, const char *path) {
    (void)ctx;
    return fl_kq_storage_default()->exists(NULL, path);
}

static int fac_dir_open(void *ctx, const char *dir, fl_kq_dir_t *out) {
    fs_dir_ref_t ref;

    if (fm_dir_open(svc_of(ctx), dir, &ref) != 0)
        return -1;
    out->fd = ref.fd;
    out->dir = ref.dir;
    return 0;
}

static void fac_dir_close(void *ctx, fl_kq_dir_t *d) {
    fs_dir_ref_t ref;

    ref.fd = d->fd;
    ref.dir = d->dir;
    fm_dir_close(svc_of(ctx), &ref);
    d->fd = -1;
}

static fs_dir_ref_t ref_of(const fl_kq_dir_t *d) {
    fs_dir_ref_t ref;

    ref.fd = d->fd;
    ref.dir = d->dir;
    return ref;
}

static int fac_dir_read(void *ctx, const fl_kq_dir_t *d, const char *name, size_t max,
                        uint8_t **data, size_t *len) {
    fs_dir_ref_t ref = ref_of(d);

    return fm_dir_read(svc_of(ctx), &ref, name, max, data, len);
}

static int fac_dir_exists(void *ctx, const fl_kq_dir_t *d, const char *name) {
    fs_dir_ref_t ref = ref_of(d);

    return fm_dir_exists(svc_of(ctx), &ref, name);
}

static int fac_dir_read_tail(void *ctx, const fl_kq_dir_t *d, const char *name, size_t unit,
                             size_t keep, uint8_t **data, size_t *len) {
    fs_dir_ref_t ref = ref_of(d);

    return fm_dir_read_tail(svc_of(ctx), &ref, name, unit, keep, data, len);
}

static int fac_dir_commit(void *ctx, const fl_kq_dir_t *d, const fl_kq_file_out_t *files,
                          size_t n) {
    fs_dir_ref_t ref = ref_of(d);
    fs_dir_file_t out[4];
    size_t i;

    if (!files || n == 0 || n > 4u)
        return -1;
    for (i = 0; i < n; i++) {
        out[i].name = files[i].name;
        out[i].data = files[i].data;
        out[i].len = files[i].len;
    }
    return fm_dir_commit(svc_of(ctx), &ref, out, n);
}

const fl_kq_storage_t *fl_kq_storage_facade(file_manager_service_t *svc, fl_kq_storage_t *out) {
    if (!svc || !out)
        return NULL;
    out->ctx = svc;
    out->read_file = fac_read_file;
    out->write_atomic = fac_write_atomic;
    out->exists = fac_exists;
    out->dir_open = fac_dir_open;
    out->dir_close = fac_dir_close;
    out->dir_read = fac_dir_read;
    out->dir_exists = fac_dir_exists;
    out->dir_read_tail = fac_dir_read_tail;
    out->dir_commit = fac_dir_commit;
    return out;
}

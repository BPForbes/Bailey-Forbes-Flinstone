/*
 * In-memory fl_kq_storage_t (kq_mem_storage.h). Written for the freestanding image: no stdio, no errno, no
 * string.h beyond strlen (kq_platform.c supplies it there). Every byte copy, zeroing and equality goes
 * through the ASM-backed mem_domain_* wrappers (asm_mem_copy / asm_mem_zero / asm_mem_ct_equal), the
 * same primitives on x86_64 NASM, x86_64 GAS and ARM GAS; file bytes are wiped with the secure variant.
 */
#include "kq_mem_storage.h"

#include "mem_domain.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    int used;
    char path[FL_KQ_MEM_PATH_MAX];
    uint8_t *data; /* never NULL while used, even for an empty file */
    size_t len;
} mem_file_t;

static mem_file_t s_files[FL_KQ_MEM_FILES_MAX];
static char s_dirs[FL_KQ_MEM_DIRS_MAX][FL_KQ_MEM_PATH_MAX];
static size_t s_bytes;
static size_t s_limit = FL_KQ_MEM_BYTES_MAX;

void fl_kq_storage_memory_set_limit(size_t bytes) {
    s_limit = bytes > FL_KQ_MEM_BYTES_MAX ? FL_KQ_MEM_BYTES_MAX : bytes;
}

size_t fl_kq_storage_memory_bytes(void) {
    return s_bytes;
}

size_t fl_kq_storage_memory_files(void) {
    size_t n = 0;

    for (size_t i = 0; i < FL_KQ_MEM_FILES_MAX; i++)
        n += s_files[i].used ? 1u : 0u;
    return n;
}

static void drop_bytes(uint8_t *p, size_t len) {
    if (!p)
        return;
    mem_domain_secure_zero(p, len ? len : 1);
    mem_domain_free(MEM_DOMAIN_FS, p);
}

void fl_kq_storage_memory_reset(void) {
    for (size_t i = 0; i < FL_KQ_MEM_FILES_MAX; i++) {
        if (s_files[i].used)
            drop_bytes(s_files[i].data, s_files[i].len);
        mem_domain_zero(&s_files[i], sizeof s_files[i]);
    }
    mem_domain_zero(s_dirs, sizeof s_dirs);
    s_bytes = 0;
    s_limit = FL_KQ_MEM_BYTES_MAX;
}

static mem_file_t *find(const char *path) {
    size_t n = strlen(path);

    for (size_t i = 0; i < FL_KQ_MEM_FILES_MAX; i++)
        if (s_files[i].used && strlen(s_files[i].path) == n && mem_domain_equal(s_files[i].path, path, n))
            return &s_files[i];
    return NULL;
}

/* A plain name: not empty, no separator, not "." or "..". */
static int plain(const char *name) {
    size_t n = name ? strlen(name) : 0;

    if (n == 0 || n >= FL_KQ_MEM_PATH_MAX || (n == 1 && name[0] == '.') || (n == 2 && name[0] == '.' && name[1] == '.'))
        return 0;
    for (size_t i = 0; i < n; i++)
        if (name[i] == '/')
            return 0;
    return 1;
}

static int join(const char *dir, const char *name, char *out) {
    size_t d = dir ? strlen(dir) : 0, n = name ? strlen(name) : 0;
    size_t sep;

    if (d == 0 || !plain(name))
        return -1;
    sep = dir[d - 1] == '/' ? 0u : 1u; /* a directory already ending in '/' ("/") gets no second one */
    if (d + sep + n >= FL_KQ_MEM_PATH_MAX)
        return -1;
    mem_domain_copy(out, dir, d);
    if (sep)
        out[d] = '/';
    mem_domain_copy(out + d + sep, name, n);
    out[d + sep + n] = '\0';
    return 0;
}

/* A copy of the bytes in MEM_DOMAIN_FS memory (never NULL, even for none). */
static uint8_t *copy_out(const uint8_t *data, size_t len) {
    uint8_t *p = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, len ? len : 1);

    if (p && len)
        mem_domain_copy(p, data, len);
    return p;
}

static int m_read_file(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len) {
    const mem_file_t *f;

    (void)ctx;
    if (data)
        *data = NULL;
    if (len)
        *len = 0;
    if (!path || !data || !len)
        return -1;
    f = find(path);
    if (!f)
        return -3;
    if (f->len > max)
        return -2;
    *data = copy_out(f->data, f->len);
    if (!*data)
        return -1;
    *len = f->len;
    return 0;
}

/* Replaces or creates the files in one step: every new buffer is made first, so a failure part-way changes
 * nothing; the swaps after them cannot fail. paths are full paths, distinct. */
static int put_all(const char *const *paths, const uint8_t *const *datas, const size_t *lens, size_t n) {
    uint8_t *fresh[4] = {0};
    mem_file_t *slot[4] = {0};
    size_t add = 0, sub = 0, need_free = 0;

    if (n == 0 || n > 4)
        return -1;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (strlen(paths[i]) == strlen(paths[j]) && mem_domain_equal(paths[i], paths[j], strlen(paths[i])))
                return -1; /* the same name twice: which would win? */
    for (size_t i = 0; i < n; i++) {
        slot[i] = find(paths[i]);
        add += lens[i];
        if (slot[i])
            sub += slot[i]->len;
        else
            need_free++;
    }
    if (s_bytes - sub + add > s_limit || s_bytes - sub + add < s_bytes - sub)
        return -1; /* over the byte limit (or the sum wrapped) */
    {
        size_t free_slots = 0;

        for (size_t i = 0; i < FL_KQ_MEM_FILES_MAX; i++)
            free_slots += s_files[i].used ? 0u : 1u;
        if (free_slots < need_free)
            return -1;
    }
    for (size_t i = 0; i < n; i++) {
        if (strlen(paths[i]) >= FL_KQ_MEM_PATH_MAX || !(fresh[i] = copy_out(datas[i], lens[i]))) {
            for (size_t j = 0; j < i; j++)
                drop_bytes(fresh[j], lens[j]);
            return -1;
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (!slot[i]) { /* a free slot, counted above */
            for (size_t k = 0; k < FL_KQ_MEM_FILES_MAX; k++)
                if (!s_files[k].used) {
                    slot[i] = &s_files[k];
                    slot[i]->used = 1; /* taken now, so the next new file in this call takes another */
                    slot[i]->data = NULL;
                    slot[i]->len = 0;
                    mem_domain_copy(slot[i]->path, paths[i], strlen(paths[i]) + 1);
                    break;
                }
        }
        s_bytes -= slot[i]->len;
        drop_bytes(slot[i]->data, slot[i]->len);
        slot[i]->data = fresh[i];
        slot[i]->len = lens[i];
        s_bytes += lens[i];
    }
    return 0;
}

static int m_write_atomic(void *ctx, const char *path, const uint8_t *data, size_t len) {
    const char *paths[1];
    const uint8_t *datas[1];
    size_t lens[1];

    (void)ctx;
    if (!path || !data || strlen(path) >= FL_KQ_MEM_PATH_MAX)
        return -1;
    paths[0] = path;
    datas[0] = data;
    lens[0] = len;
    return put_all(paths, datas, lens, 1);
}

static int m_exists(void *ctx, const char *path) {
    (void)ctx;
    return path && find(path) ? 1 : 0;
}

/* A directory is held by interning its name; the pointer stays valid until reset. fd is -1: no *at calls. */
static int m_dir_open(void *ctx, const char *dir, fl_kq_dir_t *out) {
    size_t n = dir ? strlen(dir) : 0;

    (void)ctx;
    if (!out || n == 0 || n + 2 >= FL_KQ_MEM_PATH_MAX)
        return -1;
    for (size_t i = 0; i < FL_KQ_MEM_DIRS_MAX; i++)
        if (s_dirs[i][0] && strlen(s_dirs[i]) == n && mem_domain_equal(s_dirs[i], dir, n)) {
            out->fd = -1;
            out->dir = s_dirs[i];
            return 0;
        }
    for (size_t i = 0; i < FL_KQ_MEM_DIRS_MAX; i++)
        if (!s_dirs[i][0]) {
            mem_domain_copy(s_dirs[i], dir, n + 1);
            out->fd = -1;
            out->dir = s_dirs[i];
            return 0;
        }
    return -1;
}

static void m_dir_close(void *ctx, fl_kq_dir_t *d) {
    (void)ctx;
    (void)d;
}

static int m_dir_read(void *ctx, const fl_kq_dir_t *d, const char *name, size_t max, uint8_t **data, size_t *len) {
    char path[FL_KQ_MEM_PATH_MAX];

    if (data)
        *data = NULL;
    if (len)
        *len = 0;
    if (!d || join(d->dir, name, path) != 0)
        return -1;
    return m_read_file(ctx, path, max, data, len);
}

static int m_dir_exists(void *ctx, const fl_kq_dir_t *d, const char *name) {
    char path[FL_KQ_MEM_PATH_MAX];

    if (!d || join(d->dir, name, path) != 0)
        return -1;
    return m_exists(ctx, path);
}

/* The newest `keep` records of `unit` bytes; an absent file is success with nothing; a size that is not a
 * whole number of records is an error (fl_fs_read_tail_at). */
static int m_dir_read_tail(void *ctx, const fl_kq_dir_t *d, const char *name, size_t unit, size_t keep,
                           uint8_t **data, size_t *len) {
    char path[FL_KQ_MEM_PATH_MAX];
    const mem_file_t *f;
    size_t want;

    (void)ctx;
    if (data)
        *data = NULL;
    if (len)
        *len = 0;
    if (!data || !len || !d || unit == 0 || keep == 0 || keep > SIZE_MAX / unit || join(d->dir, name, path) != 0)
        return -1;
    f = find(path);
    if (!f)
        return 0;
    if (f->len % unit != 0)
        return -1;
    want = f->len / unit;
    if (want > keep)
        want = keep;
    if (want == 0)
        return 0;
    *data = copy_out(f->data + (f->len - want * unit), want * unit);
    if (!*data)
        return -1;
    *len = want * unit;
    return 0;
}

static int m_dir_commit(void *ctx, const fl_kq_dir_t *d, const fl_kq_file_out_t *files, size_t n) {
    char full[4][FL_KQ_MEM_PATH_MAX];
    const char *paths[4];
    const uint8_t *datas[4];
    size_t lens[4];

    (void)ctx;
    if (!d || !files || n == 0 || n > 4)
        return -1;
    for (size_t i = 0; i < n; i++) {
        if (!files[i].data || join(d->dir, files[i].name, full[i]) != 0)
            return -1;
        paths[i] = full[i];
        datas[i] = files[i].data;
        lens[i] = files[i].len;
    }
    return put_all(paths, datas, lens, n);
}

static const fl_kq_storage_t s_memory = {
    NULL,        m_read_file,   m_write_atomic, m_exists,        m_dir_open,
    m_dir_close, m_dir_read,    m_dir_exists,   m_dir_read_tail, m_dir_commit,
};

const fl_kq_storage_t *fl_kq_storage_memory(void) {
    return &s_memory;
}

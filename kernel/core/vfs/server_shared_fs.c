#include "server_shared_fs.h"

#include "common.h"
#include "contract_p5_server_share.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FL_SERVER_SHARED_META_SUFFIX ".meta"

static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    size_t len;
    if (!path || !path[0])
        return -1;
    strncpy(tmp, path, sizeof(tmp) - 1u);
    tmp[sizeof(tmp) - 1u] = '\0';
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/')
            continue;
        tmp[i] = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST)
            return -1;
        tmp[i] = '/';
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int join_path(char *out, size_t cap, const char *a, const char *b)
{
    int r;

    if (!out || cap == 0u)
        return -1;
    if (!a || !a[0])
        r = snprintf(out, cap, "%s", b ? b : "");
    else if (a[strlen(a) - 1] == '/')
        r = snprintf(out, cap, "%s%s", a, b ? b : "");
    else
        r = snprintf(out, cap, "%s/%s", a, b ? b : "");
    if (r < 0 || (size_t)r >= cap) {
        errno = ENAMETOOLONG;
        return -ENAMETOOLONG;
    }
    return 0;
}

static fl_result_t path_component_safe(const char *s)
{
    size_t i;
    size_t len;

    if (!s || !s[0])
        return FL_RESULT_INVAL;
    if (!strcmp(s, ".") || !strcmp(s, ".."))
        return FL_RESULT_INVAL;
    len = strlen(s);
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '/' || c == '\\')
            return FL_RESULT_INVAL;
        if (!isalnum(c) && c != '-' && c != '_' && c != '.')
            return FL_RESULT_INVAL;
    }
    return FL_RESULT_OK;
}

/** Flat landing basename (allows spaces; rejects path separators). */
static fl_result_t landing_basename_safe(const char *s)
{
    size_t i;
    size_t len;

    if (!s || !s[0])
        return FL_RESULT_INVAL;
    if (!strcmp(s, ".") || !strcmp(s, ".."))
        return FL_RESULT_INVAL;
    len = strlen(s);
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '/' || c == '\\')
            return FL_RESULT_INVAL;
    }
    return FL_RESULT_OK;
}

static fl_result_t write_expiry_sidecar(const char *root,
                                        const char *landed,
                                        const fl_server_file_offer_t *offer)
{
    char meta_path[512];
    int n;
    FILE *fp;
    if (!root || !landed || !offer || offer->expires_at == 0u)
        return FL_RESULT_OK;
    n = snprintf(meta_path, sizeof(meta_path), "%s/%s%s",
                 root, landed, FL_SERVER_SHARED_META_SUFFIX);
    if (n < 0 || (size_t)n >= sizeof(meta_path))
        return FL_RESULT_INVAL;
    fp = fopen(meta_path, "w");
    if (!fp)
        return FL_RESULT_ERR;
    fprintf(fp, "share_id=%s\n", offer->share_id);
    fprintf(fp, "expires_at=%llu\n", (unsigned long long)offer->expires_at);
    fprintf(fp, "file_name=%s\n", offer->file_name);
    fclose(fp);
    return FL_RESULT_OK;
}

static fl_result_t move_landed_to_expired(const char *root,
                                          const char *expired_root,
                                          const char *landed)
{
    char src_file[512];
    char dst_file[512];
    char src_meta[512];
    char dst_meta[512];

    if (join_path(src_file, sizeof(src_file), root, landed) != 0)
        return FL_RESULT_ERR;
    if (join_path(dst_file, sizeof(dst_file), expired_root, landed) != 0)
        return FL_RESULT_ERR;
    if (rename(src_file, dst_file) != 0 && errno != ENOENT)
        return FL_RESULT_ERR;
    if (snprintf(src_meta, sizeof(src_meta), "%s/%s%s",
                 root, landed, FL_SERVER_SHARED_META_SUFFIX) < 0 ||
        snprintf(dst_meta, sizeof(dst_meta), "%s/%s%s",
                 expired_root, landed, FL_SERVER_SHARED_META_SUFFIX) < 0)
        return FL_RESULT_ERR;
    if (rename(src_meta, dst_meta) != 0 && errno != ENOENT)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t resolve_local_path(const char *local_path, char *out, size_t out_cap)
{
    char ab[4096];
    char rbuf[4096];
    char base_real[4096];
    int n;
    size_t base_len;

    if (!local_path || !local_path[0] || !out || out_cap == 0u)
        return FL_RESULT_INVAL;
    if (local_path[0] == '/' || strchr(local_path, '\\'))
        return FL_RESULT_ACCES;
    if (strstr(local_path, "/../") || strstr(local_path, "../") ||
        !strcmp(local_path, "..") || !strncmp(local_path, "../", 3))
        return FL_RESULT_ACCES;
    n = snprintf(ab, sizeof(ab), "%s/%s", g_cwd, local_path);
    if (n < 0 || (size_t)n >= sizeof(ab))
        return FL_RESULT_INVAL;
    if (realpath(g_cwd, base_real) == NULL) {
        strncpy(base_real, g_cwd, sizeof(base_real) - 1u);
        base_real[sizeof(base_real) - 1u] = '\0';
    }
    base_len = strlen(base_real);
    if (realpath(ab, rbuf) != NULL) {
        if (strncmp(rbuf, base_real, base_len) != 0 ||
            (rbuf[base_len] != '\0' && rbuf[base_len] != '/'))
            return FL_RESULT_ACCES;
        if (strlen(rbuf) >= out_cap)
            return FL_RESULT_INVAL;
        strncpy(out, rbuf, out_cap - 1u);
        out[out_cap - 1u] = '\0';
        return FL_RESULT_OK;
    }
    if (strncmp(ab, base_real, base_len) != 0 ||
        (ab[base_len] != '\0' && ab[base_len] != '/'))
        return FL_RESULT_ACCES;
    if (strlen(ab) >= out_cap)
        return FL_RESULT_INVAL;
    strncpy(out, ab, out_cap - 1u);
    out[out_cap - 1u] = '\0';
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_init(void)
{
    char root[512];
    char expired[512];
    if (join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (join_path(expired, sizeof(expired), root, FL_SERVER_SHARED_EXPIRED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (mkdir_p(root, 0700) != 0)
        return FL_RESULT_ERR;
    if (mkdir_p(expired, 0700) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static int path_has_expired_component(const char *canon)
{
    const char *needle = FL_SERVER_SHARED_DIR_NAME "/" FL_SERVER_SHARED_EXPIRED_DIR_NAME;
    if (!canon)
        return 0;
    return strstr(canon, needle) != NULL;
}

int fl_server_shared_path_is_expired_quarantine(const char *path)
{
    char ab[4096];
    char rbuf[4096];
    if (!path || !path[0])
        return 0;
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(ab))
            return 0;
        strncpy(ab, path, sizeof(ab) - 1u);
    } else {
        if (snprintf(ab, sizeof(ab), "%s/%s", g_cwd, path) < 0)
            return 0;
    }
    ab[sizeof(ab) - 1u] = '\0';
    if (realpath(ab, rbuf) != NULL)
        return path_has_expired_component(rbuf);
    return path_has_expired_component(ab);
}

fl_result_t fl_server_shared_landed_basename(const fl_server_file_offer_t *offer,
                                             char *out,
                                             size_t out_cap)
{
    int n;
    if (!offer || !out || out_cap == 0u || !offer->share_id[0] || !offer->file_name[0])
        return FL_RESULT_INVAL;
    if (path_component_safe(offer->share_id) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (landing_basename_safe(offer->file_name) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    n = snprintf(out, out_cap, "%zu$%s$%zu$%s",
                 strlen(offer->share_id), offer->share_id,
                 strlen(offer->file_name), offer->file_name);
    if (n < 0 || (size_t)n >= out_cap)
        return FL_RESULT_INVAL;
    if (landing_basename_safe(out) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_save_offer(const fl_server_file_offer_t *offer,
                                        const uint8_t *data,
                                        size_t data_len,
                                        char *out_path,
                                        size_t out_path_cap)
{
    char root[512];
    char landed[FL_SERVER_SHARED_LANDED_MAX];
    char file_path[512];
    FILE *fp;
    fl_result_t rc;
    if (!offer || !offer->share_id[0] || !offer->file_name[0])
        return FL_RESULT_INVAL;
    rc = fl_server_shared_landed_basename(offer, landed, sizeof(landed));
    if (rc != FL_RESULT_OK)
        return rc;
    if (fl_server_shared_init() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (join_path(file_path, sizeof(file_path), root, landed) != 0)
        return FL_RESULT_ERR;
    fp = fopen(file_path, "wb");
    if (!fp)
        return FL_RESULT_ERR;
    if (data_len > 0u && data &&
        fwrite(data, 1, data_len, fp) != data_len) {
        fclose(fp);
        return FL_RESULT_ERR;
    }
    fclose(fp);
    rc = write_expiry_sidecar(root, landed, offer);
    if (rc != FL_RESULT_OK) {
        (void)remove(file_path);
        return rc;
    }
    if (out_path && out_path_cap > 0u) {
        strncpy(out_path, file_path, out_path_cap - 1u);
        out_path[out_path_cap - 1u] = '\0';
    }
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_overwrite_local(const fl_server_file_offer_t *offer,
                                             const char *local_path,
                                             const uint8_t *data,
                                             size_t data_len)
{
    char safe_path[512];
    char parent[512];
    char *slash;
    FILE *fp;
    fl_result_t rc;

    (void)offer;
    if (!local_path || !local_path[0])
        return FL_RESULT_INVAL;
    rc = resolve_local_path(local_path, safe_path, sizeof(safe_path));
    if (rc != FL_RESULT_OK)
        return rc;
    if (fl_server_shared_path_is_expired_quarantine(safe_path))
        return FL_RESULT_ACCES;
    strncpy(parent, safe_path, sizeof(parent) - 1u);
    parent[sizeof(parent) - 1u] = '\0';
    slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (mkdir_p(parent, 0700) != 0 && errno != EEXIST)
            return FL_RESULT_ERR;
    }
    fp = fopen(safe_path, "wb");
    if (!fp)
        return FL_RESULT_ERR;
    if (data_len > 0u && data &&
        fwrite(data, 1, data_len, fp) != data_len) {
        fclose(fp);
        return FL_RESULT_ERR;
    }
    fclose(fp);
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_purge_expired(uint64_t now)
{
    char root[512];
    char expired_root[512];
    char meta_path[512];
    DIR *d;
    struct dirent *ent;
    size_t suffix_len = strlen(FL_SERVER_SHARED_META_SUFFIX);

    if (fl_server_shared_init() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (join_path(expired_root, sizeof(expired_root), root,
                  FL_SERVER_SHARED_EXPIRED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (mkdir_p(expired_root, 0700) != 0 && errno != EEXIST)
        return FL_RESULT_ERR;
    d = opendir(root);
    if (!d)
        return FL_RESULT_ERR;
    while ((ent = readdir(d)) != NULL) {
        char line[256];
        char landed[FL_SERVER_SHARED_LANDED_MAX];
        uint64_t expires_at = 0u;
        size_t name_len;
        FILE *fp;
        if (ent->d_name[0] == '.')
            continue;
        if (!strcmp(ent->d_name, FL_SERVER_SHARED_EXPIRED_DIR_NAME))
            continue;
        name_len = strlen(ent->d_name);
        if (name_len <= suffix_len ||
            strcmp(ent->d_name + name_len - suffix_len,
                   FL_SERVER_SHARED_META_SUFFIX) != 0)
            continue;
        if (name_len - suffix_len >= sizeof(landed))
            continue;
        memcpy(landed, ent->d_name, name_len - suffix_len);
        landed[name_len - suffix_len] = '\0';
        join_path(meta_path, sizeof(meta_path), root, ent->d_name);
        fp = fopen(meta_path, "r");
        if (!fp)
            continue;
        expires_at = 0u;
        while (fgets(line, sizeof(line), fp)) {
            unsigned long long v;
            if (sscanf(line, "expires_at=%llu", &v) == 1)
                expires_at = (uint64_t)v;
        }
        fclose(fp);
        if (expires_at != 0u && now > expires_at)
            (void)move_landed_to_expired(root, expired_root, landed);
    }
    closedir(d);
    return FL_RESULT_OK;
}

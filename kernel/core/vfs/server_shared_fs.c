#include "server_shared_fs.h"

#include "common.h"
#include "contract_p5_server_share.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void join_path(char *out, size_t cap, const char *a, const char *b)
{
    if (!out || cap == 0u)
        return;
    if (!a || !a[0]) {
        snprintf(out, cap, "%s", b ? b : "");
        return;
    }
    if (a[strlen(a) - 1] == '/')
        snprintf(out, cap, "%s%s", a, b ? b : "");
    else
        snprintf(out, cap, "%s/%s", a, b ? b : "");
}

fl_result_t fl_server_shared_init(void)
{
    char root[512];
    char expired[512];
    join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME);
    join_path(expired, sizeof(expired), root, FL_SERVER_SHARED_EXPIRED_DIR_NAME);
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

static fl_result_t write_meta(const char *dir, const fl_server_file_offer_t *offer)
{
    char meta[512];
    FILE *fp;
    if (!dir || !offer)
        return FL_RESULT_INVAL;
    join_path(meta, sizeof(meta), dir, "offer.meta");
    fp = fopen(meta, "w");
    if (!fp)
        return FL_RESULT_ERR;
    fprintf(fp, "share_id=%s\n", offer->share_id);
    fprintf(fp, "expires_at=%llu\n", (unsigned long long)offer->expires_at);
    fprintf(fp, "file_name=%s\n", offer->file_name);
    fclose(fp);
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_save_offer(const fl_server_file_offer_t *offer,
                                        const uint8_t *data,
                                        size_t data_len,
                                        char *out_path,
                                        size_t out_path_cap)
{
    char root[512];
    char dir[512];
    char file_path[512];
    FILE *fp;
    if (!offer || !offer->share_id[0] || !offer->file_name[0])
        return FL_RESULT_INVAL;
    if (fl_server_shared_init() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME);
    join_path(dir, sizeof(dir), root, offer->share_id);
    if (mkdir_p(dir, 0700) != 0)
        return FL_RESULT_ERR;
    join_path(file_path, sizeof(file_path), dir, offer->file_name);
    fp = fopen(file_path, "wb");
    if (!fp)
        return FL_RESULT_ERR;
    if (data_len > 0u && data &&
        fwrite(data, 1, data_len, fp) != data_len) {
        fclose(fp);
        return FL_RESULT_ERR;
    }
    fclose(fp);
    if (write_meta(dir, offer) != FL_RESULT_OK)
        return FL_RESULT_ERR;
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
    char parent[512];
    char *slash;
    FILE *fp;
    if (!offer || !local_path || !local_path[0])
        return FL_RESULT_INVAL;
    if (fl_server_shared_path_is_expired_quarantine(local_path))
        return FL_RESULT_ACCES;
    strncpy(parent, local_path, sizeof(parent) - 1u);
    parent[sizeof(parent) - 1u] = '\0';
    slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (mkdir_p(parent, 0700) != 0 && errno != EEXIST)
            return FL_RESULT_ERR;
    }
    fp = fopen(local_path, "wb");
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

static fl_result_t move_dir_to_expired(const char *share_id)
{
    char root[512];
    char expired_root[512];
    char src[512];
    char dst[512];
    if (!share_id || !share_id[0])
        return FL_RESULT_INVAL;
    join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME);
    join_path(expired_root, sizeof(expired_root), root, FL_SERVER_SHARED_EXPIRED_DIR_NAME);
    join_path(src, sizeof(src), root, share_id);
    join_path(dst, sizeof(dst), expired_root, share_id);
    if (mkdir_p(expired_root, 0700) != 0 && errno != EEXIST)
        return FL_RESULT_ERR;
    if (rename(src, dst) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_purge_expired(uint64_t now)
{
    char root[512];
    char meta_path[512];
    DIR *d;
    struct dirent *ent;
    if (fl_server_shared_init() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME);
    d = opendir(root);
    if (!d)
        return FL_RESULT_ERR;
    while ((ent = readdir(d)) != NULL) {
        char dir_path[512];
        uint64_t expires_at = 0u;
        FILE *fp;
        char line[256];
        if (ent->d_name[0] == '.')
            continue;
        if (!strcmp(ent->d_name, FL_SERVER_SHARED_EXPIRED_DIR_NAME))
            continue;
        join_path(dir_path, sizeof(dir_path), root, ent->d_name);
        join_path(meta_path, sizeof(meta_path), dir_path, "offer.meta");
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
            (void)move_dir_to_expired(ent->d_name);
    }
    closedir(d);
    return FL_RESULT_OK;
}

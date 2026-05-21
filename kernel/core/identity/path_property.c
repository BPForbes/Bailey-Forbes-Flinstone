#include "path_property.h"
#include "mem_domain.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int read_props_file(const char *file, fl_path_property_t *out) {
    FILE *f;
    char line[256];
    if (!file || !out)
        return -1;
    mem_domain_zero(out, sizeof(*out));
    f = fopen(file, "r");
    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        const char *p;
        if (strstr(line, "\"owner\"")) {
            p = strchr(line, ':');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p == '"') {
                    char *q = strchr(p + 1, '"');
                    size_t n;
                    if (q) {
                        n = (size_t)(q - (p + 1));
                        if (n >= sizeof(out->owner))
                            n = sizeof(out->owner) - 1;
                        memcpy(out->owner, p + 1, n);
                        out->owner[n] = '\0';
                    }
                }
            }
        } else if (strstr(line, "\"requires_elevation\"")) {
            int v = 0;
            p = strchr(line, ':');
            if (p) {
                v = (int)strtol(p + 1, NULL, 10);
                out->requires_elevation = (v != 0) ? 1 : 0;
            }
        }
    }
    fclose(f);
    out->valid = (out->owner[0] != '\0' || out->requires_elevation) ? 1 : 0;
    return out->valid ? 0 : -1;
}

static int meta_path_for(const char *dir, char *buf, size_t bufsz) {
    int n = snprintf(buf, bufsz, "%s/%s", dir, FL_PATH_META_FILE);
    return (n > 0 && (size_t)n < bufsz) ? 0 : -1;
}

fl_result_t fl_path_property_resolve(const char *path, fl_path_property_t *out) {
    char canon[PATH_MAX];
    char walk[PATH_MAX];
    char meta[PATH_MAX + 64];
    char *slash;
    struct stat st;
    fl_path_property_t best;

    if (!path || !out)
        return FL_RESULT_INVAL;
    mem_domain_zero(out, sizeof(*out));
    mem_domain_zero(&best, sizeof(best));

    if (!realpath(path, canon)) {
        if (path[0] == '/')
            strncpy(canon, path, sizeof(canon) - 1);
        else
            return FL_RESULT_NOENT;
        canon[sizeof(canon) - 1] = '\0';
    }

    strncpy(walk, canon, sizeof(walk) - 1);
    walk[sizeof(walk) - 1] = '\0';

    for (;;) {
        const char *probe = walk;
        if (stat(probe, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (meta_path_for(probe, meta, sizeof(meta)) == 0) {
                fl_path_property_t cur;
                if (read_props_file(meta, &cur) == 0) {
                    if (!best.valid || cur.requires_elevation || cur.owner[0])
                        best = cur;
                }
            }
        }
        slash = strrchr(walk, '/');
        if (!slash || slash == walk)
            break;
        *slash = '\0';
    }

    if (best.valid) {
        *out = best;
        return FL_RESULT_OK;
    }
    return FL_RESULT_NOENT;
}

fl_result_t fl_path_property_set_dir(const char *dir_path, const char *owner, int requires_elevation) {
    char dir_meta[PATH_MAX + 32];
    char file[PATH_MAX + 64];
    FILE *f;
    int n;

    if (!dir_path)
        return FL_RESULT_INVAL;
    n = snprintf(dir_meta, sizeof(dir_meta), "%s/%s", dir_path, FL_PATH_META_DIR);
    if (n <= 0 || (size_t)n >= sizeof(dir_meta))
        return FL_RESULT_INVAL;
    if (mkdir(dir_meta, 0755) != 0 && errno != EEXIST)
        return FL_RESULT_ERR;
    if (meta_path_for(dir_path, file, sizeof(file)) != 0)
        return FL_RESULT_INVAL;
    f = fopen(file, "w");
    if (!f)
        return FL_RESULT_ERR;
    fprintf(f, "{\n  \"owner\": \"%s\",\n  \"requires_elevation\": %d\n}\n",
            owner ? owner : "", requires_elevation ? 1 : 0);
    fclose(f);
    return FL_RESULT_OK;
}

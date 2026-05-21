/* TODO(contract-mount/GH-124): path_property.h anchors contract_p2_principal.h + contract_p2_authz.h. */
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

static int json_escape_string(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    if (!in || !out || outsz < 2)
        return -1;
    if (o + 1 >= outsz)
        return -1;
    out[o++] = '"';
    for (const char *p = in; *p; p++) {
        const char *rep = NULL;
        char scratch[7];
        size_t rlen = 0;
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            scratch[0] = '\\';
            scratch[1] = *p;
            scratch[2] = '\0';
            rep = scratch;
            rlen = 2;
        } else if (c < 0x20) {
            snprintf(scratch, sizeof(scratch), "\\u%04x", c);
            rep = scratch;
            rlen = 6;
        } else {
            if (o + 1 >= outsz)
                return -1;
            out[o++] = *p;
            continue;
        }
        if (o + rlen + 1 >= outsz)
            return -1;
        memcpy(out + o, rep, rlen);
        o += rlen;
    }
    if (o + 2 >= outsz)
        return -1;
    out[o++] = '"';
    out[o] = '\0';
    return 0;
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
        if (!slash)
            break;
        if (slash == walk) {
            if (meta_path_for("/", meta, sizeof(meta)) == 0) {
                fl_path_property_t cur;
                if (read_props_file(meta, &cur) == 0) {
                    if (!best.valid || cur.requires_elevation || cur.owner[0])
                        best = cur;
                }
            }
            break;
        }
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
    char owner_json[FL_PATH_OWNER_MAX * 6 + 4];
    f = fopen(file, "w");
    if (!f)
        return FL_RESULT_ERR;
    if (json_escape_string(owner ? owner : "", owner_json, sizeof(owner_json)) != 0) {
        fclose(f);
        return FL_RESULT_ERR;
    }
    fprintf(f, "{\n  \"owner\": %s,\n  \"requires_elevation\": %d\n}\n",
            owner_json, requires_elevation ? 1 : 0);
    fclose(f);
    return FL_RESULT_OK;
}

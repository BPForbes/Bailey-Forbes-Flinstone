/*
 * Generate userland/shell/version_changelog.c from version/locked files ending in .ver
 * (finalized release notes) and the highest A.B.C among them for the headline string.
 * Used by GitHub Actions before make CHANGELOG_CI=1.
 *
 * Each .ver file (UTF-8 text) contains lines such as:
 *   MAJOR_VERSION=2
 *   STANDARD_VERSION=2
 *   RELEASE_VERSION=4        (aliases: MINOR_VERSION, VERSION_PATCH)
 *   RELEASE_DATE=2026-05-11  (optional; YYYY-MM-DD. If omitted, changelog uses
 *                             the generator's local calendar date via time(3).)
 *   DESCRIPTION=One-line release notes (value may be quoted)
 *   DESCRIPTION<<DELIM      (heredoc: following lines until a line equal to
 *   ... arbitrary text ...   DELIM after trim, become the description; newlines kept)
 *   DELIM
 * Lines may optionally start with "int " before the key. Lines starting with #
 * are comments. Empty lines are ignored outside heredocs.
 *
 * Build: gcc -std=c11 -Wall -Wextra -O2 -o gen_version_changelog scripts/gen_version_changelog.c
 * Run:   ./gen_version_changelog [repo_root]
 *
 * If there are no .ver files, falls back to parsing VERSION_* from userland/shell/version_def.h.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/*
 * Create leading components (mkdir -p semantics). Path must be mutable.
 */
static int mkdir_p(char *path) {
    size_t len = strlen(path);
    if (len == 0)
        return -1;
    for (size_t i = 1; i < len; i++) {
        if (path[i] == '/') {
            path[i] = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST)
                return -1;
            path[i] = '/';
        }
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

typedef struct {
    char *p;
    size_t len;
    size_t cap;
} Buf;

static int buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 512;
        while (b->len + extra + 1 > ncap)
            ncap *= 2;
        char *np = realloc(b->p, ncap);
        if (!np)
            return -1;
        b->p = np;
        b->cap = ncap;
    }
    return 0;
}

static int buf_append_bytes(Buf *b, const char *s, size_t n) {
    if (buf_reserve(b, n) != 0)
        return -1;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 0;
}

static int buf_append_cstr(Buf *b, const char *s) {
    return buf_append_bytes(b, s, strlen(s));
}

static int buf_append_fmt(Buf *b, const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return -1;
    if (n >= (int)sizeof tmp)
        return -1;
    if (n == 0)
        return 0;
    return buf_append_bytes(b, tmp, (size_t)n);
}

static void buf_free(Buf *b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

static char *read_entire_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void trim_cpy(const char *src, char *dst, size_t dstsz) {
    const char *a = src;
    while (*a && isspace((unsigned char)*a))
        a++;
    const char *b = a + strlen(a);
    while (b > a && isspace((unsigned char)b[-1]))
        b--;
    size_t n = (size_t)(b - a);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, a, n);
    dst[n] = '\0';
}

static int parse_triplet(const char *text, int *ma, int *st, int *pa) {
    *ma = *st = *pa = -1;
    char *copy = strdup(text);
    if (!copy)
        return 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char trimmed[512];
        trim_cpy(line, trimmed, sizeof trimmed);
        if (!strstr(trimmed, "#define"))
            continue;
        int v;
        if (strstr(trimmed, "VERSION_MAJOR") &&
            sscanf(trimmed, "#define VERSION_MAJOR %d", &v) == 1)
            *ma = v;
        if (strstr(trimmed, "VERSION_STANDARD") &&
            sscanf(trimmed, "#define VERSION_STANDARD %d", &v) == 1)
            *st = v;
        if (strstr(trimmed, "VERSION_PATCH") &&
            sscanf(trimmed, "#define VERSION_PATCH %d", &v) == 1)
            *pa = v;
    }
    free(copy);
    return *ma >= 0 && *st >= 0 && *pa >= 0;
}

typedef struct {
    int ma, st, rel;
    char *desc; /* malloc; multiline allowed */
    char date_iso[16]; /* YYYY-MM-DD from file, or empty → use generator date */
    char relpath[PATH_MAX];
    int valid;
} VerEntry;

static void ver_list_free(VerEntry *list, size_t nent) {
    if (!list)
        return;
    for (size_t j = 0; j < nent; j++)
        free(list[j].desc);
    free(list);
}

static const char *skip_int_kw(const char *s) {
    const char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (strncmp(p, "int", 3) == 0 && isspace((unsigned char)p[3]))
        return p + 4;
    return s;
}

static int match_key(const char *line, const char *key, const char **val_out) {
    const char *p = skip_int_kw(line);
    size_t len = strlen(key);
    if (strncmp(p, key, len) != 0)
        return 0;
    if (p[len] != '=')
        return 0;
    *val_out = p + len + 1;
    return 1;
}

static void trim_value(char *s, size_t sz) {
    char tmp[4096];
    trim_cpy(s, tmp, sizeof tmp);
    size_t L = strlen(tmp);
    if (L >= 2 && tmp[0] == '"' && tmp[L - 1] == '"') {
        tmp[L - 1] = '\0';
        memmove(tmp, tmp + 1, strlen(tmp) + 1);
    }
    snprintf(s, sz, "%s", tmp);
}

static int parse_positive_int(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > 999999)
        return -1;
    *out = (int)v;
    return 0;
}

/* Strict calendar YYYY-MM-DD (same shape as merge-time stamp). */
static int release_date_iso_valid(const char *s) {
    if (!s)
        return 0;
    if (strlen(s) != 10)
        return 0;
    for (size_t i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-')
                return 0;
        } else if (!isdigit((unsigned char)s[i]))
            return 0;
    }
    int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 +
            (s[3] - '0');
    int m = (s[5] - '0') * 10 + (s[6] - '0');
    int d = (s[8] - '0') * 10 + (s[9] - '0');
    if (y < 1970 || y > 9999)
        return 0;
    if (m < 1 || m > 12)
        return 0;
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxd = mdays[m - 1];
    if (m == 2) {
        int leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        maxd = leap ? 29 : 28;
    }
    if (d < 1 || d > maxd)
        return 0;
    return 1;
}

static void free_line_array(char **lines, size_t n) {
    if (!lines)
        return;
    for (size_t i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
}

/* Split file into malloc'd lines (without trailing \n or \r). */
static int split_file_lines(const char *raw, char ***out_lines, size_t *out_n) {
    size_t cap = 32, n = 0;
    char **arr = calloc(cap, sizeof *arr);
    if (!arr)
        return -1;
    const char *p = raw;
    while (*p) {
        const char *a = p;
        while (*p && *p != '\n' && *p != '\r')
            p++;
        size_t len = (size_t)(p - a);
        char *ln = malloc(len + 1u);
        if (!ln) {
            free_line_array(arr, n);
            return -1;
        }
        memcpy(ln, a, len);
        ln[len] = '\0';
        if (n + 1 >= cap) {
            cap *= 2;
            char **na = realloc(arr, cap * sizeof *na);
            if (!na) {
                free(ln);
                free_line_array(arr, n);
                return -1;
            }
            arr = na;
        }
        arr[n++] = ln;
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;
    }
    *out_lines = arr;
    *out_n = n;
    return 0;
}

static void resolve_entry_date_iso(const VerEntry *e, char *dst, size_t dstsz) {
    if (e->date_iso[0]) {
        snprintf(dst, dstsz, "%s", e->date_iso);
        return;
    }
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm || strftime(dst, dstsz, "%Y-%m-%d", tm) == 0)
        snprintf(dst, dstsz, "1970-01-01");
}

static int parse_ver_file(const char *fullpath, const char *relpath, VerEntry *e) {
    memset(e, 0, sizeof *e);
    e->desc = NULL;
    char **lines = NULL;
    size_t nlines = 0;
    snprintf(e->relpath, sizeof e->relpath, "%s", relpath);

    char *raw = read_entire_file(fullpath);
    if (!raw)
        return -1;

    if (split_file_lines(raw, &lines, &nlines) != 0) {
        free(raw);
        return -1;
    }
    free(raw);

    int ma = -1, st = -1, rel = -1;
    char date_iso[16] = "";
    char *desc = NULL;

    size_t i = 0;
    while (i < nlines) {
        char trim[4096];
        trim_cpy(lines[i], trim, sizeof trim);
        if (!trim[0] || trim[0] == '#') {
            i++;
            continue;
        }

        const char *sk = skip_int_kw(trim);

        if (strncmp(sk, "DESCRIPTION<<", 13) == 0) {
            const char *rest = sk + 13;
            while (*rest && isspace((unsigned char)*rest))
                rest++;
            char delim[256];
            trim_cpy(rest, delim, sizeof delim);
            if (!delim[0]) {
                fprintf(stderr,
                        "gen_version_changelog: DESCRIPTION<< needs delimiter token in %s\n",
                        fullpath);
                goto bad;
            }
            i++;
            Buf acc = {0};
            int found = 0;
            while (i < nlines) {
                char tcl[4096];
                trim_cpy(lines[i], tcl, sizeof tcl);
                if (!strcmp(tcl, delim)) {
                    i++;
                    found = 1;
                    break;
                }
                if (acc.len) {
                    if (buf_append_cstr(&acc, "\n") != 0) {
                        fprintf(stderr,
                                "gen_version_changelog: out of memory accumulating "
                                "DESCRIPTION in %s\n",
                                fullpath);
                        buf_free(&acc);
                        goto bad;
                    }
                }
                if (buf_append_cstr(&acc, lines[i]) != 0) {
                    fprintf(stderr,
                            "gen_version_changelog: out of memory accumulating "
                            "DESCRIPTION in %s\n",
                            fullpath);
                    buf_free(&acc);
                    goto bad;
                }
                i++;
            }
            if (!found) {
                fprintf(stderr, "gen_version_changelog: unclosed DESCRIPTION<<%s in %s\n",
                        delim, fullpath);
                buf_free(&acc);
                goto bad;
            }
            free(desc);
            if (acc.p) {
                desc = acc.p;
                acc.p = NULL;
            } else {
                desc = strdup("");
                if (!desc) {
                    buf_free(&acc);
                    goto bad;
                }
            }
            buf_free(&acc);
            continue;
        }

        const char *val = NULL;
        char work[16384];

        if (match_key(trim, "MAJOR_VERSION", &val) ||
            match_key(trim, "VERSION_MAJOR", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (parse_positive_int(work, &ma) != 0)
                goto bad;
            i++;
            continue;
        }
        if (match_key(trim, "STANDARD_VERSION", &val) ||
            match_key(trim, "VERSION_STANDARD", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (parse_positive_int(work, &st) != 0)
                goto bad;
            i++;
            continue;
        }
        if (match_key(trim, "RELEASE_VERSION", &val) ||
            match_key(trim, "MINOR_VERSION", &val) ||
            match_key(trim, "VERSION_PATCH", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (parse_positive_int(work, &rel) != 0)
                goto bad;
            i++;
            continue;
        }
        if (match_key(trim, "RELEASE_DATE", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (strlen(work) >= sizeof date_iso) {
                fprintf(stderr, "gen_version_changelog: RELEASE_DATE too long in %s\n",
                        fullpath);
                goto bad;
            }
            if (!release_date_iso_valid(work)) {
                fprintf(stderr,
                        "gen_version_changelog: RELEASE_DATE must be YYYY-MM-DD in %s\n",
                        fullpath);
                goto bad;
            }
            snprintf(date_iso, sizeof date_iso, "%s", work);
            i++;
            continue;
        }
        if (match_key(trim, "DESCRIPTION", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            free(desc);
            desc = strdup(work);
            if (!desc)
                goto bad;
            i++;
            continue;
        }

        i++;
    }

    free_line_array(lines, nlines);
    lines = NULL;

    if (ma < 0 || st < 0 || rel < 0 || !desc || !desc[0]) {
        fprintf(stderr,
                "gen_version_changelog: incomplete entry %s (need MAJOR/STANDARD/"
                "RELEASE or MINOR/PATCH + DESCRIPTION)\n",
                fullpath);
        free(desc);
        return -1;
    }

    e->ma = ma;
    e->st = st;
    e->rel = rel;
    e->desc = desc;
    snprintf(e->date_iso, sizeof e->date_iso, "%s", date_iso);
    e->valid = 1;
    return 0;

bad:
    free(desc);
    if (lines)
        free_line_array(lines, nlines);
    return -1;
}

static int append_escaped(Buf *out, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '\\') {
            if (buf_append_cstr(out, "\\\\") != 0)
                return -1;
        } else if (c == '"') {
            if (buf_append_cstr(out, "\\\"") != 0)
                return -1;
        } else if (c == '\r')
            continue;
        else if (c == '\n') {
            if (buf_append_cstr(out, "\\n") != 0)
                return -1;
        } else if (c >= 32 && c < 127) {
            if (buf_append_fmt(out, "%c", c) != 0)
                return -1;
        } else {
            if (buf_append_fmt(out, "\\x%02x", c) != 0)
                return -1;
        }
    }
    return 0;
}

static int cmp_entry_desc(const void *a, const void *b) {
    const VerEntry *ea = a;
    const VerEntry *eb = b;
    if (ea->ma != eb->ma)
        return eb->ma - ea->ma;
    if (ea->st != eb->st)
        return eb->st - ea->st;
    if (ea->rel != eb->rel)
        return eb->rel - ea->rel;
    return strcmp(ea->relpath, eb->relpath);
}

static int gen_oom_cleanup(Buf *hdr, Buf *bd, Buf *ent, VerEntry *lst, size_t n) {
    if (ent)
        buf_free(ent);
    buf_free(hdr);
    buf_free(bd);
    ver_list_free(lst, n);
    fprintf(stderr, "gen_version_changelog: out of memory\n");
    return 1;
}

int main(int argc, char **argv) {
    char root[PATH_MAX];
    const char *arg = (argc > 1) ? argv[1] : ".";
    if (!realpath(arg, root)) {
        fprintf(stderr, "gen_version_changelog: realpath failed for %s\n", arg);
        return 1;
    }

    char vdef[PATH_MAX];
    char outpath[PATH_MAX];
    char shelldir[PATH_MAX];

    if (snprintf(vdef, sizeof vdef, "%s/userland/shell/version_def.h", root) >=
        (int)sizeof vdef) {
        fprintf(stderr, "gen_version_changelog: path too long\n");
        return 1;
    }
    if (snprintf(outpath, sizeof outpath, "%s/userland/shell/version_changelog.c",
                 root) >= (int)sizeof outpath) {
        fprintf(stderr, "gen_version_changelog: path too long\n");
        return 1;
    }
    if (snprintf(shelldir, sizeof shelldir, "%s/userland/shell", root) >=
        (int)sizeof shelldir) {
        fprintf(stderr, "gen_version_changelog: path too long\n");
        return 1;
    }
    char locked_dir[PATH_MAX];
    if (snprintf(locked_dir, sizeof locked_dir, "%s/version/locked", root) >=
        (int)sizeof locked_dir) {
        fprintf(stderr, "gen_version_changelog: path too long\n");
        return 1;
    }

    char *vtxt = NULL;
    int ma = 0, st = 0, pa = 0;

    VerEntry *list = NULL;
    size_t nent = 0;

    DIR *ed = opendir(locked_dir);
    if (!ed) {
        if (errno != ENOENT) {
            fprintf(stderr, "gen_version_changelog: cannot open %s: %s\n",
                    locked_dir, strerror(errno));
            return 1;
        }
    } else {
        struct dirent *de;
        while ((de = readdir(ed)) != NULL) {
            const char *name = de->d_name;
            size_t nl = strlen(name);
            if (nl < 5 || strcmp(name + nl - 4, ".ver") != 0)
                continue;
            char full[PATH_MAX];
            char rel[PATH_MAX];
            if (snprintf(full, sizeof full, "%s/%s", locked_dir, name) >=
                    (int)sizeof full ||
                snprintf(rel, sizeof rel, "version/locked/%s", name) >=
                    (int)sizeof rel) {
                fprintf(stderr, "gen_version_changelog: path too long\n");
                closedir(ed);
                ver_list_free(list, nent);
                return 1;
            }
            VerEntry ent;
            if (parse_ver_file(full, rel, &ent) != 0) {
                closedir(ed);
                ver_list_free(list, nent);
                return 1;
            }
            VerEntry *np = realloc(list, (nent + 1) * sizeof *np);
            if (!np) {
                closedir(ed);
                free(ent.desc);
                ver_list_free(list, nent);
                fprintf(stderr, "gen_version_changelog: out of memory\n");
                return 1;
            }
            list = np;
            list[nent++] = ent;
        }
        closedir(ed);
    }

    if (nent > 0) {
        qsort(list, nent, sizeof *list, cmp_entry_desc);
        ma = list[0].ma;
        st = list[0].st;
        pa = list[0].rel;
    } else {
        vtxt = read_entire_file(vdef);
        if (!vtxt || !*vtxt) {
            fprintf(stderr,
                    "gen_version_changelog: no version/locked/*.ver and cannot read %s\n",
                    vdef);
            free(vtxt);
            ver_list_free(list, nent);
            return 1;
        }
        if (!parse_triplet(vtxt, &ma, &st, &pa)) {
            fprintf(stderr, "gen_version_changelog: parse VERSION_* failed for %s\n", vdef);
            free(vtxt);
            ver_list_free(list, nent);
            return 1;
        }
        free(vtxt);
        vtxt = NULL;
    }

    char ver_buf[64];
    snprintf(ver_buf, sizeof ver_buf, "%d.%d.%d", ma, st, pa);

    char headline_date[16];
    if (nent > 0) {
        resolve_entry_date_iso(&list[0], headline_date, sizeof headline_date);
    } else {
        VerEntry hd = {0};
        resolve_entry_date_iso(&hd, headline_date, sizeof headline_date);
    }

    Buf header_line = {0};
    Buf body = {0};

    if (buf_append_fmt(&header_line,
                       "%s (%s) - changelog from version/locked (*.ver); see AGENTS.md\n",
                       ver_buf, headline_date) != 0)
        return gen_oom_cleanup(&header_line, &body, NULL, list, nent);

    if (buf_append_cstr(&body,
                        "/* Generated by scripts/gen_version_changelog.c — do not edit "
                        "by hand */\n\n") != 0 ||
        buf_append_cstr(&body, "#include \"version_def.h\"\n\n") != 0 ||
        buf_append_cstr(&body, "const char VERSION_CHANGELOG[] =\n") != 0 ||
        buf_append_cstr(&body, "    \"") != 0 ||
        append_escaped(&body, header_line.p ? header_line.p : "") != 0 ||
        buf_append_cstr(&body, "\"\n") != 0)
        return gen_oom_cleanup(&header_line, &body, NULL, list, nent);
    buf_free(&header_line);

    if (nent > 0) {
        for (size_t i = 0; i < nent; i++) {
            VerEntry *e = &list[i];
            char dbuf[16];
            resolve_entry_date_iso(e, dbuf, sizeof dbuf);
            Buf entrybuf = {0};
            if (buf_append_fmt(&entrybuf, "%d.%d.%d (%s)\n", e->ma, e->st, e->rel, dbuf) != 0 ||
                buf_append_cstr(&entrybuf, e->desc) != 0 ||
                buf_append_cstr(&entrybuf, "\n(") != 0 ||
                buf_append_cstr(&entrybuf, e->relpath) != 0 ||
                buf_append_cstr(&entrybuf, ")\n") != 0 ||
                buf_append_cstr(&body, "    \"") != 0 ||
                append_escaped(&body, entrybuf.p ? entrybuf.p : "") != 0 ||
                buf_append_cstr(&body, "\"\n") != 0)
                return gen_oom_cleanup(&header_line, &body, &entrybuf, list, nent);
            buf_free(&entrybuf);
        }
    } else {
        if (buf_append_cstr(&body, "    \"") != 0 ||
            append_escaped(&body,
                           "(no version/locked/*.ver files — finalize from version/entries first)\n") !=
                0 ||
            buf_append_cstr(&body, "\"\n") != 0)
            return gen_oom_cleanup(&header_line, &body, NULL, list, nent);
    }

    ver_list_free(list, nent);
    list = NULL;
    nent = 0;

    if (buf_append_cstr(&body, ";\n") != 0) {
        buf_free(&body);
        fprintf(stderr, "gen_version_changelog: out of memory\n");
        return 1;
    }

    {
        char dir_copy[PATH_MAX];
        if (snprintf(dir_copy, sizeof dir_copy, "%s", shelldir) >= (int)sizeof dir_copy) {
            fprintf(stderr, "gen_version_changelog: path too long\n");
            buf_free(&body);
            return 1;
        }
        if (mkdir_p(dir_copy) != 0) {
            fprintf(stderr, "gen_version_changelog: cannot create %s\n", shelldir);
            buf_free(&body);
            return 1;
        }
    }

    FILE *out = fopen(outpath, "wb");
    if (!out) {
        fprintf(stderr, "gen_version_changelog: cannot write %s\n", outpath);
        buf_free(&body);
        return 1;
    }
    fwrite(body.p, 1, body.len, out);
    fclose(out);
    buf_free(&body);

    fprintf(stderr, "gen_version_changelog: wrote %s\n", outpath);
    return 0;
}

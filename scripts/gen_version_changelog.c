/*
 * Generate userland/shell/version_changelog.c from version_def.h + git log.
 * Used by GitHub Actions before make CHANGELOG_CI=1.
 *
 * Build: gcc -std=c11 -Wall -Wextra -O2 -o gen_version_changelog scripts/gen_version_changelog.c
 * Run:   ./gen_version_changelog [repo_root]
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

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

static void buf_append_bytes(Buf *b, const char *s, size_t n) {
    if (buf_reserve(b, n) != 0)
        return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_append_cstr(Buf *b, const char *s) {
    buf_append_bytes(b, s, strlen(s));
}

static void buf_append_fmt(Buf *b, const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0 && n < (int)sizeof tmp)
        buf_append_bytes(b, tmp, (size_t)n);
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

static int only_whitespace(const char *s) {
    for (; *s; s++) {
        if (!isspace((unsigned char)*s))
            return 0;
    }
    return 1;
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

static void append_escaped(Buf *out, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '\\')
            buf_append_cstr(out, "\\\\");
        else if (c == '"')
            buf_append_cstr(out, "\\\"");
        else if (c == '\r')
            continue;
        else if (c == '\n')
            buf_append_cstr(out, "\\n");
        else if (c >= 32 && c < 127)
            buf_append_fmt(out, "%c", c);
        else
            buf_append_fmt(out, "\\x%02x", c);
    }
}

static int run_git_log(const char *root, Buf *acc) {
    char cmd[PATH_MAX + 128];
    if (snprintf(cmd, sizeof cmd,
                 "git -C \"%s\" log --no-merges -48 "
                 "--pretty=format:'%%h %%s'",
                 root) >= (int)sizeof cmd)
        return -1;
    FILE *p = popen(cmd, "r");
    if (!p)
        return -1;
    char linebuf[8192];
    while (fgets(linebuf, sizeof linebuf, p)) {
        buf_append_cstr(acc, linebuf);
    }
    int st = pclose(p);
    return st == 0 ? 0 : -1;
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

    char *vtxt = read_entire_file(vdef);
    if (!vtxt || !*vtxt) {
        fprintf(stderr, "gen_version_changelog: cannot read %s\n", vdef);
        free(vtxt);
        return 1;
    }

    int ma = 0, st = 0, pa = 0;
    if (!parse_triplet(vtxt, &ma, &st, &pa)) {
        fprintf(stderr, "gen_version_changelog: parse VERSION_* failed\n");
        free(vtxt);
        return 1;
    }
    free(vtxt);

    Buf git_acc = {0};
    if (run_git_log(root, &git_acc) != 0) {
        buf_free(&git_acc);
        buf_append_cstr(&git_acc, "(no git history available)\n");
    }

    if (only_whitespace(git_acc.p ? git_acc.p : "")) {
        buf_free(&git_acc);
        buf_append_cstr(&git_acc, "(empty git log)\n");
    }

    char ver_buf[64];
    snprintf(ver_buf, sizeof ver_buf, "%d.%d.%d", ma, st, pa);

    Buf header_line = {0};
    buf_append_cstr(&header_line, ver_buf);
    buf_append_cstr(&header_line, " — CI-assembled changelog (recent commits)\n");

    Buf body = {0};
    buf_append_cstr(&body,
                    "/* Generated by scripts/gen_version_changelog.c — do not edit "
                    "by hand */\n\n");
    buf_append_cstr(&body, "#include \"version_def.h\"\n\n");
    buf_append_cstr(&body, "const char VERSION_CHANGELOG[] =\n");
    buf_append_cstr(&body, "    \"");
    append_escaped(&body, header_line.p ? header_line.p : "");
    buf_append_cstr(&body, "\"\n");
    buf_free(&header_line);

    char *gcopy = strdup(git_acc.p ? git_acc.p : "");
    buf_free(&git_acc);
    if (!gcopy) {
        buf_free(&body);
        fprintf(stderr, "gen_version_changelog: out of memory\n");
        return 1;
    }

    char *save = NULL;
    for (char *line = strtok_r(gcopy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char trimmed[4096];
        trim_cpy(line, trimmed, sizeof trimmed);
        if (!trimmed[0])
            continue;
        buf_append_cstr(&body, "    \"");
        append_escaped(&body, trimmed);
        buf_append_cstr(&body, "\\n\"\n");
    }
    free(gcopy);

    buf_append_cstr(&body, ";\n");

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

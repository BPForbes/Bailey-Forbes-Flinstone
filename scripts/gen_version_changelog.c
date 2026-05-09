/*
 * Generate userland/shell/version_changelog.c from version_def.h and
 * version/entries files ending in .ver (release notes). Used by GitHub Actions before
 * make CHANGELOG_CI=1.
 *
 * Each .ver file (UTF-8 text) contains lines such as:
 *   MAJOR_VERSION=2
 *   STANDARD_VERSION=2
 *   RELEASE_VERSION=4        (aliases: MINOR_VERSION, VERSION_PATCH)
 *   DESCRIPTION=A short release note (<= 1023 chars, single line)
 * Lines may optionally start with "int " before the key. Lines starting with #
 * are comments. Empty lines are ignored.
 *
 * Build: gcc -std=c11 -Wall -Wextra -O2 -o gen_version_changelog scripts/gen_version_changelog.c
 * Run:   ./gen_version_changelog [repo_root]
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

/**
 * Create all leading directories for a mutable path using "mkdir -p" semantics.
 *
 * Attempts to create each directory component of `path` (up to the full path)
 * with permission mode 0755. `path` must be writable by the caller because it
 * may be modified temporarily during operation.
 *
 * @param path Mutable NUL-terminated path whose leading components will be created.
 * @returns `0` on success, `-1` on failure (errno is set by the failing syscall).  
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

/**
 * Ensure the buffer has capacity for `extra` additional bytes plus a terminating NUL.
 *
 * On success the buffer's capacity will be at least `b->len + extra + 1`. The function
 * may realloc `b->p` and update `b->cap` (initial growth starts at 512 and then doubles).
 *
 * @param b Pointer to the buffer to grow.
 * @param extra Number of additional bytes required.
 * @returns `0` on success, `-1` if memory allocation fails (buffer state is unchanged on failure).
 */
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

/**
 * Append `n` bytes from `s` to the growable buffer `b`, ensuring a NUL terminator.
 *
 * Appends exactly `n` bytes from `s` to `b`. If reserving space fails, the buffer
 * is left unchanged. On success `b->len` is increased by `n` and `b->p[b->len]`
 * is set to `'\0'`.
 *
 * @param b Destination growable buffer.
 * @param s Source bytes to append.
 * @param n Number of bytes to append from `s`.
 */
static void buf_append_bytes(Buf *b, const char *s, size_t n) {
    if (buf_reserve(b, n) != 0)
        return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

/**
 * Append a NUL-terminated string to a growable buffer.
 *
 * Appends the characters of `s` up to but not including the terminating NUL to `b`.
 * The buffer remains NUL-terminated after the operation.
 *
 * @param b Buffer to append into; must be initialized.
 * @param s NUL-terminated string whose contents will be appended.
 */
static void buf_append_cstr(Buf *b, const char *s) {
    buf_append_bytes(b, s, strlen(s));
}

/**
 * Format a string with the given `fmt` and arguments and append it to `b`
 * when the formatted result fits entirely within an internal 4096-byte buffer.
 *
 * If formatting produces a non-empty string and its length is less than 4096
 * bytes, the resulting bytes are appended to `b`. If the formatted output is
 * empty or would be truncated, nothing is appended.
 *
 * @param b Buffer to append formatted text to.
 * @param fmt printf-style format string followed by corresponding arguments.
 */
static void buf_append_fmt(Buf *b, const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0 && n < (int)sizeof tmp)
        buf_append_bytes(b, tmp, (size_t)n);
}

/**
 * Free the buffer's internal storage and reset its fields to an empty state.
 *
 * This releases the heap memory held in `b->p` but does not free the `Buf`
 * structure itself.
 *
 * @param b Buffer whose internal storage will be freed and fields reset.
 */
static void buf_free(Buf *b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

/**
 * Read an entire file into a newly allocated NUL-terminated buffer.
 *
 * Opens the file at `path`, reads its full contents into a heap buffer, NUL-terminates
 * the buffer, and returns the pointer to that buffer.
 *
 * @param path Path to the file to read.
 * @returns Pointer to a heap-allocated, NUL-terminated buffer containing the file contents,
 *          or `NULL` on any error (e.g., open/read/allocation failure).
 * @note The caller is responsible for freeing the returned buffer with `free()` when no longer needed.
 */
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

/**
 * Copy src into dst, removing leading and trailing whitespace and NUL-terminating the result.
 *
 * Copies the trimmed substring of `src` into `dst`. If the trimmed content length is
 * greater than or equal to `dstsz`, the result is truncated to `dstsz - 1` bytes.
 * The destination buffer is always NUL-terminated.
 *
 * @param src Source NUL-terminated string.
 * @param dst Destination buffer to receive the trimmed string.
 * @param dstsz Size of the destination buffer in bytes; must be at least 1.
 */
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

/**
 * Parse a C header text blob for VERSION_MAJOR, VERSION_STANDARD, and VERSION_PATCH and write their integer values to the provided outputs.
 *
 * Each output (`ma`, `st`, `pa`) is initialized to -1 before parsing; on success all three are set to the parsed integers.
 *
 * @param text Pointer to the NUL-terminated input text to scan (treated as immutable).
 * @param ma Output pointer that receives the parsed `VERSION_MAJOR` or remains -1 if not found.
 * @param st Output pointer that receives the parsed `VERSION_STANDARD` or remains -1 if not found.
 * @param pa Output pointer that receives the parsed `VERSION_PATCH` or remains -1 if not found.
 * @returns `1` if all three version values were found and written to the output pointers, `0` otherwise.
 */
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
    char desc[1024];
    char relpath[PATH_MAX];
    int valid;
} VerEntry;

/**
 * Skip leading whitespace and an optional "int" keyword followed by whitespace.
 *
 * @param s Input C string that may begin with whitespace and an optional `int` token.
 * @returns Pointer to the first character after leading whitespace and the optional `int` plus one following whitespace; returns the original `s` if the optional `int` token is not present.
 */
static const char *skip_int_kw(const char *s) {
    const char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (strncmp(p, "int", 3) == 0 && isspace((unsigned char)p[3]))
        return p + 4;
    return s;
}

/**
 * Determine whether a line (optionally prefixed with the token `int`) begins with `key=`
 * and, if so, provide a pointer to the value portion after the '='.
 *
 * @param line Input line to inspect; may start with whitespace and an optional `int` token.
 * @param key Key to match at the start of the (possibly adjusted) line.
 * @param val_out Receives a pointer into `line` at the first character after '=' when a match is found.
 * @returns `1` if `key=` is found (and `*val_out` is set), `0` otherwise.
 */
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

/**
 * Normalize a value string by trimming leading/trailing whitespace and removing
 * surrounding double quotes if both the first and last characters are '"'.
 *
 * The resulting string is written back into `s`, always NUL-terminated and
 * truncated to fit `sz` bytes if necessary.
 *
 * @param s Buffer containing the input string; receives the normalized result.
 * @param sz Size of the buffer `s` in bytes.
 */
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

/**
 * Parse a decimal non-negative integer in the range 0 through 999999 from `s`.
 *
 * @param s NUL-terminated string containing the integer text to parse.
 * @param out Pointer to an int where the parsed value will be stored on success.
 * @returns 0 on successful parse and range validation, -1 if the string is not a valid decimal integer or the value is outside the allowed range.
 */
static int parse_positive_int(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > 999999)
        return -1;
    *out = (int)v;
    return 0;
}

/**
 * Parse a .ver release-entry file and populate a VerEntry record.
 *
 * Reads and parses the file at `fullpath`, recording `relpath` into the
 * entry. Extracts numeric version fields (MAJOR/STANDARD/RELEASE or
 * MINOR/PATCH) and a single-line DESCRIPTION; on success fills `*e`,
 * null-terminates strings, sets `e->valid = 1`, and returns 0.
 *
 * @param fullpath Filesystem path to the .ver file to parse.
 * @param relpath  Relative path used for the entry's relpath metadata.
 * @param e        Output pointer to a VerEntry to initialize on success.
 * @returns 0 on successful parse and population of `*e`; -1 on failure.
 *          On failure `*e` is zeroed. DESCRIPTION longer than 1023
 *          characters or missing required fields will cause failure.
 */
static int parse_ver_file(const char *fullpath, const char *relpath, VerEntry *e) {
    memset(e, 0, sizeof *e);
    snprintf(e->relpath, sizeof e->relpath, "%s", relpath);

    char *raw = read_entire_file(fullpath);
    if (!raw)
        return -1;

    int ma = -1, st = -1, rel = -1;
    char desc[1024];
    desc[0] = '\0';

    char *save = NULL;
    for (char *line = strtok_r(raw, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char trim[4096];
        trim_cpy(line, trim, sizeof trim);
        if (!trim[0] || trim[0] == '#')
            continue;

        const char *val = NULL;
        char work[4096];

        if (match_key(trim, "MAJOR_VERSION", &val) ||
            match_key(trim, "VERSION_MAJOR", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (parse_positive_int(work, &ma) != 0)
                goto bad;
            continue;
        }
        if (match_key(trim, "STANDARD_VERSION", &val) ||
            match_key(trim, "VERSION_STANDARD", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (parse_positive_int(work, &st) != 0)
                goto bad;
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
            continue;
        }
        if (match_key(trim, "DESCRIPTION", &val)) {
            strncpy(work, val, sizeof work - 1);
            work[sizeof work - 1] = '\0';
            trim_value(work, sizeof work);
            if (strlen(work) > 1023) {
                fprintf(stderr, "gen_version_changelog: DESCRIPTION too long in %s\n",
                        fullpath);
                goto bad;
            }
            snprintf(desc, sizeof desc, "%s", work);
            continue;
        }
    }

    free(raw);

    if (ma < 0 || st < 0 || rel < 0 || !desc[0]) {
        fprintf(stderr,
                "gen_version_changelog: incomplete entry %s (need MAJOR/STANDARD/"
                "RELEASE or MINOR/PATCH + DESCRIPTION)\n",
                fullpath);
        return -1;
    }

    e->ma = ma;
    e->st = st;
    e->rel = rel;
    memcpy(e->desc, desc, sizeof e->desc);
    e->desc[sizeof e->desc - 1] = '\0';
    e->valid = 1;
    return 0;

bad:
    free(raw);
    return -1;
}

/**
 * Append a C-string-safe, escaped representation of `s` to the buffer `out` for embedding inside a C string literal.
 *
 * Characters are translated as follows:
 * - '\\' → "\\\\"
 * - '"'  → "\\\""
 * - '\r' → removed
 * - '\n' → "\\n"
 * - ASCII bytes 32..126 → copied as-is
 * - all other bytes → "\\x%02x" hex escape
 *
 * @param out Destination buffer to append the escaped text to.
 * @param s   NUL-terminated input string to escape.
 */
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

/**
 * Compare two VerEntry records for ordering release entries.
 *
 * @param a Pointer to the first VerEntry (passed as a `const void *` from qsort).
 * @param b Pointer to the second VerEntry (passed as a `const void *` from qsort).
 * @returns Negative if `a` should sort before `b`, positive if `a` should sort after `b`, `0` if they are equivalent.
 *          Ordering is by descending `ma`, then descending `st`, then descending `rel`, then ascending `relpath`.
 */
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

/**
 * Generate userland/shell/version_changelog.c from version_def.h and the
 * release-note files in version/entries under the specified repository root.
 *
 * Parses VERSION_MAJOR / VERSION_STANDARD / VERSION_PATCH from
 * userland/shell/version_def.h, scans version/entries for *.ver files,
 * extracts and sorts release entries, then emits a generated C source file
 * containing a single C-string initializer `const char VERSION_CHANGELOG[]`
 * with an escaped header line and one quoted line per entry.
 *
 * @param argc Number of command-line arguments; an optional repository root
 *             path may be provided as argv[1].
 * @param argv Argument vector; argv[1], if present, is treated as the
 *             repository root directory (defaults to ".").
 * @returns `0` on success, `1` on any error (path or IO errors, parse failures,
 *          allocation failures, or inability to create the target directory).
 */
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
    char entries_dir[PATH_MAX];
    if (snprintf(entries_dir, sizeof entries_dir, "%s/version/entries", root) >=
        (int)sizeof entries_dir) {
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

    VerEntry *list = NULL;
    size_t nent = 0;

    DIR *ed = opendir(entries_dir);
    if (!ed) {
        if (errno != ENOENT) {
            fprintf(stderr, "gen_version_changelog: cannot open %s: %s\n",
                    entries_dir, strerror(errno));
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
            if (snprintf(full, sizeof full, "%s/%s", entries_dir, name) >=
                    (int)sizeof full ||
                snprintf(rel, sizeof rel, "version/entries/%s", name) >=
                    (int)sizeof rel) {
                fprintf(stderr, "gen_version_changelog: path too long\n");
                closedir(ed);
                free(list);
                return 1;
            }
            VerEntry ent;
            if (parse_ver_file(full, rel, &ent) != 0) {
                closedir(ed);
                free(list);
                return 1;
            }
            VerEntry *np = realloc(list, (nent + 1) * sizeof *np);
            if (!np) {
                closedir(ed);
                free(list);
                fprintf(stderr, "gen_version_changelog: out of memory\n");
                return 1;
            }
            list = np;
            list[nent++] = ent;
        }
        closedir(ed);
    }

    char ver_buf[64];
    snprintf(ver_buf, sizeof ver_buf, "%d.%d.%d", ma, st, pa);

    Buf header_line = {0};
    buf_append_cstr(&header_line, ver_buf);
    buf_append_cstr(&header_line,
                    " — changelog from version/entries (*.ver); see "
                    "AGENTS.md\n");

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

    if (nent > 0) {
        qsort(list, nent, sizeof *list, cmp_entry_desc);
        for (size_t i = 0; i < nent; i++) {
            VerEntry *e = &list[i];
            char lineout[2048];
            snprintf(lineout, sizeof lineout, "%d.%d.%d: %s (%s)\n", e->ma, e->st,
                     e->rel, e->desc, e->relpath);
            buf_append_cstr(&body, "    \"");
            append_escaped(&body, lineout);
            buf_append_cstr(&body, "\"\n");
        }
    } else {
        buf_append_cstr(&body, "    \"");
        append_escaped(&body,
                       "(no version/entries/*.ver files — add release notes there)\n");
        buf_append_cstr(&body, "\"\n");
    }

    free(list);

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

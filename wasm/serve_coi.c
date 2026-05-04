/*
 * Minimal static HTTP server for local WASM testing.
 * Adds Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy so
 * Emscripten pthread builds get SharedArrayBuffer.
 *
 * Usage: wasm/serve_coi [port]
 * Default port 8080. Serves files from the directory containing this binary
 * (typically .../wasm/ after `make wasm-serve`).
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const char kCoop[] = "Cross-Origin-Opener-Policy: same-origin\r\n";
static const char kCoep[] = "Cross-Origin-Embedder-Policy: require-corp\r\n";

struct mime_entry {
    const char *ext;
    const char *type;
};

static const struct mime_entry kMime[] = {
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".mjs", "application/javascript; charset=utf-8"},
    {".wasm", "application/wasm"},
    {".json", "application/json"},
    {".css", "text/css; charset=utf-8"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".txt", "text/plain; charset=utf-8"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
};

static const char *mime_type_for_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "application/octet-stream";
    char ext[32];
    size_t i = 0;
    for (const char *p = dot; *p && i + 1 < sizeof ext; p++) {
        ext[i++] = (char)tolower((unsigned char)*p);
    }
    ext[i] = '\0';
    for (size_t j = 0; j < sizeof kMime / sizeof kMime[0]; j++) {
        if (strcmp(ext, kMime[j].ext) == 0)
            return kMime[j].type;
    }
    return "application/octet-stream";
}

#define SERVE_PATH_CAP 8192

#define MAX_SEG 128
#define SEG_LEN 256

/* Normalize relative path (no leading slash): drop ".", collapse ".." vs prior segment. */
static int lexical_normalize_rel(const char *in, char *out, size_t outsz) {
    char segs[MAX_SEG][SEG_LEN];
    int n = 0;

    const char *p = in;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t L = (size_t)(p - start);
        if (L == 0)
            continue;
        if (L >= SEG_LEN)
            return -1;

        char seg[SEG_LEN];
        memcpy(seg, start, L);
        seg[L] = '\0';

        if (strcmp(seg, ".") == 0)
            continue;
        if (strcmp(seg, "..") == 0) {
            if (n > 0 && strcmp(segs[n - 1], "..") != 0)
                n--;
            else {
                if (n >= MAX_SEG)
                    return -1;
                memcpy(segs[n], seg, L + 1);
                n++;
            }
        } else {
            if (n >= MAX_SEG)
                return -1;
            memcpy(segs[n], seg, L + 1);
            n++;
        }
    }

    if (n == 0) {
        if (outsz < 1)
            return -1;
        out[0] = '\0';
        return 0;
    }

    size_t off = 0;
    for (int i = 0; i < n; i++) {
        size_t sl = strlen(segs[i]);
        if (off + 1 + sl >= outsz)
            return -1;
        if (i > 0)
            out[off++] = '/';
        memcpy(out + off, segs[i], sl);
        off += sl;
    }
    out[off] = '\0';
    return 0;
}

static bool path_has_prefix(const char *path, const char *prefix) {
    size_t lp = strlen(prefix);
    if (strncmp(path, prefix, lp) != 0)
        return false;
    return path[lp] == '\0' || path[lp] == '/';
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t nw = send(fd, buf + off, len - off, 0);
        if (nw <= 0)
            return -1;
        off += (size_t)nw;
    }
    return 0;
}

static int read_file_binary(const char *path, char **out_buf, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[(size_t)sz] = '\0';
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

static void strip_path_query(char *path) {
    char *q = strpbrk(path, "?#");
    if (q)
        *q = '\0';
}

int main(int argc, char *argv[]) {
    unsigned short port = 8080;
    if (argc >= 2) {
        char *end = NULL;
        unsigned long p = strtoul(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || p == 0 || p > 65535) {
            fprintf(stderr, "usage: %s [port]\n", argv[0]);
            return 1;
        }
        port = (unsigned short)p;
    }

    char doc_root[PATH_MAX];
    char argv_copy[PATH_MAX];
    if (strlen(argv[0]) >= sizeof argv_copy) {
        fprintf(stderr, "path too long\n");
        return 1;
    }
    memcpy(argv_copy, argv[0], strlen(argv[0]) + 1);
    char *parent = dirname(argv_copy);
    if (!realpath(parent, doc_root)) {
        if (!getcwd(doc_root, sizeof doc_root)) {
            perror("getcwd");
            return 1;
        }
    }

    char doc_root_canon[PATH_MAX];
    if (!realpath(doc_root, doc_root_canon)) {
        perror("realpath doc_root");
        return 1;
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        perror("setsockopt");
        close(srv);
        return 1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(srv);
        return 1;
    }
    if (listen(srv, 8) != 0) {
        perror("listen");
        close(srv);
        return 1;
    }

    printf("Serving %s at http://127.0.0.1:%u/ (and on all interfaces; COOP+COEP for "
           "WASM pthreads)\n",
           doc_root_canon, (unsigned)port);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr *)&cli, &clen);
        if (fd < 0) {
            perror("accept");
            continue;
        }

        char req[65536];
        size_t reqlen = 0;
        for (;;) {
            if (reqlen >= sizeof(req) - 1) {
                reqlen = 0;
                break;
            }
            ssize_t n = recv(fd, req + reqlen, sizeof(req) - 1 - reqlen, 0);
            if (n <= 0)
                break;
            reqlen += (size_t)n;
            req[reqlen] = '\0';
            if (strstr(req, "\r\n\r\n"))
                break;
        }

        char method[16], raw_path[SERVE_PATH_CAP], ver[32];
        if (sscanf(req, "%15s %8191s %31s", method, raw_path, ver) < 2) {
            method[0] = '\0';
            raw_path[0] = '\0';
        }

        bool is_head = strcmp(method, "HEAD") == 0;
        if (strcmp(method, "GET") != 0 && !is_head) {
            static const char bad[] = "HTTP/1.1 405 Method Not Allowed\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            char hdr[512];
            int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", bad, kCoop, kCoep);
            if (hlen > 0 && (size_t)hlen < sizeof hdr)
                (void)send_all(fd, hdr, (size_t)hlen);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            continue;
        }

        strip_path_query(raw_path);

        if (raw_path[0] != '/') {
            static const char bad[] = "HTTP/1.1 400 Bad Request\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            char hdr[512];
            int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", bad, kCoop, kCoep);
            if (hlen > 0 && (size_t)hlen < sizeof hdr)
                (void)send_all(fd, hdr, (size_t)hlen);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            continue;
        }

        char rel_norm[PATH_MAX];
        if (lexical_normalize_rel(raw_path + 1, rel_norm, sizeof rel_norm) != 0 ||
            rel_norm[0] == '\0' || rel_norm[0] == '/') {
            static const char bad[] = "HTTP/1.1 403 Forbidden\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            char hdr[512];
            int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", bad, kCoop, kCoep);
            if (hlen > 0 && (size_t)hlen < sizeof hdr)
                (void)send_all(fd, hdr, (size_t)hlen);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            continue;
        }

        char target[PATH_MAX * 2];
        int tl = snprintf(target, sizeof target, "%s/%s", doc_root_canon, rel_norm);
        if (tl < 0 || (size_t)tl >= sizeof target) {
            static const char bad[] = "HTTP/1.1 403 Forbidden\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            char hdr[512];
            int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", bad, kCoop, kCoep);
            if (hlen > 0 && (size_t)hlen < sizeof hdr)
                (void)send_all(fd, hdr, (size_t)hlen);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            continue;
        }

        struct stat st;
        if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
            tl = snprintf(target, sizeof target, "%s/%s/index.html", doc_root_canon, rel_norm);
            if (tl < 0 || (size_t)tl >= sizeof target) {
                static const char nf[] = "HTTP/1.1 404 Not Found\r\n"
                                         "Content-Type: text/plain; charset=utf-8\r\n"
                                         "Content-Length: 9\r\n"
                                         "Connection: close\r\n";
                char hdr[512];
                int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", nf, kCoop, kCoep);
                if (hlen > 0 && (size_t)hlen < sizeof hdr)
                    (void)send_all(fd, hdr, (size_t)hlen);
                if (!is_head)
                    (void)send_all(fd, "not found", 9);
                shutdown(fd, SHUT_RDWR);
                close(fd);
                continue;
            }
        }

        char resolved[PATH_MAX];
        if (!realpath(target, resolved) || !path_has_prefix(resolved, doc_root_canon) ||
            stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
            static const char nf[] = "HTTP/1.1 404 Not Found\r\n"
                                     "Content-Type: text/plain; charset=utf-8\r\n"
                                     "Content-Length: 9\r\n"
                                     "Connection: close\r\n";
            char hdr[512];
            int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", nf, kCoop, kCoep);
            if (hlen > 0 && (size_t)hlen < sizeof hdr)
                (void)send_all(fd, hdr, (size_t)hlen);
            if (!is_head)
                (void)send_all(fd, "not found", 9);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            continue;
        }

        char *body = NULL;
        size_t body_len = 0;
        if (read_file_binary(resolved, &body, &body_len) != 0) {
            static const char nf[] = "HTTP/1.1 404 Not Found\r\n"
                                     "Content-Type: text/plain; charset=utf-8\r\n"
                                     "Content-Length: 9\r\n"
                                     "Connection: close\r\n";
            char hdr[512];
            int hlen = snprintf(hdr, sizeof hdr, "%s%s%s\r\n", nf, kCoop, kCoep);
            if (hlen > 0 && (size_t)hlen < sizeof hdr)
                (void)send_all(fd, hdr, (size_t)hlen);
            if (!is_head)
                (void)send_all(fd, "not found", 9);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            continue;
        }

        const char *mime = mime_type_for_path(resolved);
        char hdr[1024];
        int hlen = snprintf(hdr, sizeof hdr,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n"
                            "%s"
                            "%s"
                            "\r\n",
                            mime, body_len, kCoop, kCoep);
        if (hlen > 0 && (size_t)hlen < sizeof hdr && send_all(fd, hdr, (size_t)hlen) == 0) {
            if (!is_head && body_len > 0)
                (void)send_all(fd, body, body_len);
        }
        free(body);
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

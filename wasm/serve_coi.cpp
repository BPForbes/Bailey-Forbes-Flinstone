/*
 * Minimal static HTTP server for local WASM testing.
 * Adds Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy so
 * Emscripten pthread builds get SharedArrayBuffer.
 *
 * Usage: wasm/serve_coi [port]
 * Default port 8080. Serves files from the directory containing this binary
 * (typically .../wasm/ after `make wasm-serve`).
 */

#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

static const char kCoop[] = "Cross-Origin-Opener-Policy: same-origin\r\n";
static const char kCoep[] = "Cross-Origin-Embedder-Policy: require-corp\r\n";

static std::string mime_type(const fs::path &p) {
    static const std::unordered_map<std::string, std::string> m = {
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
    std::string ext = p.extension().string();
    for (auto &c : ext)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    auto it = m.find(ext);
    if (it != m.end())
        return it->second;
    return "application/octet-stream";
}

static bool is_subpath(const fs::path &root, const fs::path &candidate) {
    std::error_code ec;
    fs::path R = fs::weakly_canonical(root, ec);
    if (ec)
        return false;
    fs::path P = fs::weakly_canonical(candidate, ec);
    if (ec)
        return false;
    auto r = R.begin(), re = R.end();
    auto p = P.begin(), pe = P.end();
    for (; r != re; ++r, ++p) {
        if (p == pe || *r != *p)
            return false;
    }
    return true;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, buf + off, len - off, 0);
        if (n <= 0)
            return -1;
        off += static_cast<size_t>(n);
    }
    return 0;
}

static int send_string(int fd, const std::string &s) {
    return send_all(fd, s.data(), s.size());
}

static std::string read_file_binary(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

int main(int argc, char *argv[]) {
    unsigned short port = 8080;
    if (argc >= 2) {
        char *end = nullptr;
        unsigned long p = std::strtoul(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || p == 0 || p > 65535) {
            std::cerr << "usage: " << argv[0] << " [port]\n";
            return 1;
        }
        port = static_cast<unsigned short>(p);
    }

    fs::path doc_root;
    try {
        doc_root = fs::canonical(fs::path(argv[0]).parent_path());
    } catch (...) {
        doc_root = fs::current_path();
    }

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        perror("setsockopt");
        ::close(srv);
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(srv, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        perror("bind");
        ::close(srv);
        return 1;
    }
    if (::listen(srv, 8) != 0) {
        perror("listen");
        ::close(srv);
        return 1;
    }

    std::cout << "Serving " << doc_root.string() << " at http://127.0.0.1:" << port
              << "/ (and on all interfaces; COOP+COEP for WASM pthreads)\n";

    for (;;) {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        int fd = ::accept(srv, reinterpret_cast<sockaddr *>(&cli), &clen);
        if (fd < 0) {
            perror("accept");
            continue;
        }

        std::string req;
        char buf[4096];
        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            req.append(buf, static_cast<size_t>(n));
            if (req.find("\r\n\r\n") != std::string::npos)
                break;
        }

        std::istringstream line_in(req);
        std::string method, raw_path, ver;
        line_in >> method >> raw_path >> ver;

        bool is_head = (method == "HEAD");
        if (method != "GET" && method != "HEAD") {
            static const char bad[] = "HTTP/1.1 405 Method Not Allowed\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            (void)send_string(fd, std::string(bad) + kCoop + kCoep + "\r\n");
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            continue;
        }

        if (raw_path.empty() || raw_path[0] != '/') {
            static const char bad[] = "HTTP/1.1 400 Bad Request\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            (void)send_string(fd, std::string(bad) + kCoop + kCoep + "\r\n");
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            continue;
        }

        fs::path rel = fs::path(raw_path.substr(1)).lexically_normal();
        if (rel.empty() || rel.is_absolute()) {
            static const char bad[] = "HTTP/1.1 403 Forbidden\r\n"
                                      "Content-Length: 0\r\n"
                                      "Connection: close\r\n";
            (void)send_string(fd, std::string(bad) + kCoop + kCoep + "\r\n");
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            continue;
        }

        fs::path target = doc_root / rel;
        if (fs::is_directory(target))
            target /= "index.html";

        if (!is_subpath(doc_root, target) || !fs::is_regular_file(target)) {
            static const char nf[] = "HTTP/1.1 404 Not Found\r\n"
                                     "Content-Type: text/plain; charset=utf-8\r\n"
                                     "Content-Length: 9\r\n"
                                     "Connection: close\r\n";
            std::string body = "not found";
            std::ostringstream hdr;
            hdr << nf << kCoop << kCoep << "\r\n";
            (void)send_string(fd, hdr.str());
            if (!is_head)
                (void)send_string(fd, body);
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            continue;
        }

        std::string body = read_file_binary(target);
        std::ostringstream hdr;
        hdr << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: " << mime_type(target) << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << kCoop << kCoep << "\r\n";
        if (send_string(fd, hdr.str()) != 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            continue;
        }
        if (!is_head && !body.empty() && send_string(fd, body) != 0) {
            /* ignore */
        }
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

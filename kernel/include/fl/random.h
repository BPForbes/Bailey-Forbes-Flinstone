#ifndef FL_RANDOM_H
#define FL_RANDOM_H

/**
 * The one source of secret-grade random bytes. Header-only and stateless, so
 * every hosted target (shell, tests, the C++ password code, the wasm lab)
 * shares it without linking another object or OpenSSL.
 *
 * Hosted builds only: it needs libc and, outside Emscripten, getrandom(2) (glibc >= 2.25
 * for <sys/random.h>) or /dev/urandom. Freestanding kernel objects must not include it.
 *
 * Early boot: getrandom(2) without flags blocks until the kernel pool is initialised, so
 * it never returns weak bytes. The /dev/urandom fallback is used only when getrandom is
 * unavailable (ENOSYS, EPERM under seccomp, non-Linux); it is checked to be a character
 * device first, but a system that serves /dev/urandom before its pool is seeded is
 * outside what this header can detect. getrandom is called with flags 0, so at early boot
 * it may block until the pool is seeded; that is accepted and the behaviour is fixed.
 * The device-number check (/dev/urandom is 1:9) applies on Linux only; elsewhere the
 * fallback checks only that the node is a character device.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/sysmacros.h>
#endif
#include <unistd.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/random.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Nested on purpose: clang rejects a function-like macro in an #if even when a defined()
 * test earlier on the same line is false. */
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ) && defined(_DEFAULT_SOURCE)
#if __GLIBC_PREREQ(2, 25)
#define FL_RANDOM_HAVE_EXPLICIT_BZERO 1
#endif
#endif

/* open() follows symlinks, so S_ISCHR alone would accept any character device the path
 * resolves to. On Linux the real /dev/urandom is always device 1:9; elsewhere only the type
 * check is available. */
static inline int fl_random_urandom_rdev_ok(const struct stat *st) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return st->st_rdev == makedev(1, 9);
#else
    (void)st;
    return 1;
#endif
}

/**
 * Fill buf[0..len) with random bytes. 0 on success. -1 when buf is NULL, len is 0
 * or len > INT_MAX, or no source can deliver all of it. All or nothing: on -1 the
 * whole of buf[0..len) (when buf is non-NULL and len is valid) is zeroed. The zeroing
 * uses explicit_bzero where glibc provides it, else memset, which a compiler may drop:
 * callers that handle secrets must wipe their own buffers.
 * errno on -1 (set after the wipe, so cleanup cannot overwrite it): EINVAL for a bad
 * argument; otherwise the error of the last failing source (getrandom or open/read of
 * /dev/urandom, getentropy under Emscripten), or EIO when there was none (short or empty
 * read, not a character device).
 */
static inline int fl_random_bytes(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    int err = 0;

    if (!buf || len == 0 || len > (size_t)INT_MAX) {
        errno = EINVAL;
        return -1;
    }
#if defined(__EMSCRIPTEN__)
    while (got < len) {
        size_t n = len - got > 256 ? 256 : len - got;

        if (getentropy(p + got, n) != 0) {
            err = errno;
            goto fail;
        }
        got += n;
    }
    return 0;
#else
#if defined(__linux__)
    while (got < len) {
        ssize_t r = getrandom(p + got, len - got, 0);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            err = errno;
            break; /* ENOSYS and the like: try /dev/urandom */
        }
        if (r == 0)
            break; /* no progress: never spin */
        got += (size_t)r;
    }
    if (got == len)
        return 0;
#endif
    {
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        struct stat st;

        if (fd < 0)
            err = errno;
        if (fd >= 0 && fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) &&
            fl_random_urandom_rdev_ok(&st)) {
            while (got < len) {
                ssize_t r = read(fd, p + got, len - got);

                if (r < 0 && errno == EINTR)
                    continue;
                if (r < 0)
                    err = errno;
                if (r <= 0)
                    break;
                got += (size_t)r;
            }
            if (got == len) {
                close(fd);
                return 0;
            }
        }
        if (fd >= 0)
            close(fd);
    }
    goto fail;
#endif
fail:
#if defined(FL_RANDOM_HAVE_EXPLICIT_BZERO)
    explicit_bzero(buf, len);
#else
    memset(buf, 0, len);
#endif
    errno = err ? err : EIO;
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif

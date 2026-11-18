#ifndef FL_PATH_H
#define FL_PATH_H

/**
 * Path helpers shared by every target (they need <string.h> and snprintf; the freestanding image
 * gets both from kq_platform.c). Header-only so the kernel, shell, tests, the browser lab and the
 * image use one copy without linking another object.
 */

#include <stdio.h>
#include <string.h>
#ifndef FL_FREESTANDING
#include <errno.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Last component of path: the text after the final '/', a pointer into the
 * input (nothing is copied). No '/' returns path itself; a trailing '/'
 * returns "" so a name validator rejects it. NULL returns "". Only '/' is a
 * separator; "." and ".." come back unchanged.
 */
static inline const char *fl_fs_basename(const char *path) {
    const char *slash;

    if (!path)
        return "";
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/** Longest plain file name fl_fs_name_is_plain accepts, in bytes, excluding the terminator. */
#define FL_FS_NAME_PLAIN_MAX 127

/**
 * 1 when name is one plain file name, 0 otherwise. Not NULL, not empty, not "." or "..",
 * no '/' or '\\', no control byte (below 0x20 or 0x7F), and shorter than 128 bytes.
 * Spaces, leading dots and bytes of 0x80 and above (UTF-8) are allowed.
 */
static inline int fl_fs_name_is_plain(const char *name) {
    const unsigned char *p;

    if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
        return 0;
    for (p = (const unsigned char *)name; *p; p++)
        if (*p == '/' || *p == '\\' || *p < 0x20u || *p == 0x7Fu)
            return 0;
    return (size_t)(p - (const unsigned char *)name) <= FL_FS_NAME_PLAIN_MAX;
}

/**
 * dir "/" name into out (no separator added after a trailing '/', none before an empty dir).
 * 0, or non-zero when it does not fit (hosted: -ENAMETOOLONG with errno set).
 */
static inline int fl_fs_join_path(char *out, size_t cap, const char *dir, const char *name) {
    int r;

    if (!out || cap == 0u)
        return -1;
    if (!dir || !dir[0])
        r = snprintf(out, cap, "%s", name ? name : "");
    else if (dir[strlen(dir) - 1] == '/')
        r = snprintf(out, cap, "%s%s", dir, name ? name : "");
    else
        r = snprintf(out, cap, "%s/%s", dir, name ? name : "");
    if (r < 0 || (size_t)r >= cap) {
#ifndef FL_FREESTANDING
        errno = ENAMETOOLONG;
        return -ENAMETOOLONG;
#else
        return -1;
#endif
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif

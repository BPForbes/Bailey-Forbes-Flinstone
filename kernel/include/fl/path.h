#ifndef FL_PATH_H
#define FL_PATH_H

/**
 * Path helpers shared by every hosted target (it needs <string.h>). Header-only so the kernel,
 * shell, tests and the browser lab use one copy without linking another object.
 */

#include <string.h>

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

#ifdef __cplusplus
}
#endif

#endif

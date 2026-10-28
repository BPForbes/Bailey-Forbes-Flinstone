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

#ifdef __cplusplus
}
#endif

#endif

#ifndef FL_PLATFORM_H
#define FL_PLATFORM_H

/*
 * Host-platform detection.
 * Distinguishes WSL (Windows Subsystem for Linux), native Linux, and macOS.
 * Result is cached after the first call — safe to call freely.
 */

typedef enum {
    FL_PLATFORM_UNDETECTED = -1, /* internal sentinel; never returned to callers */
    FL_PLATFORM_UNKNOWN    =  0,
    FL_PLATFORM_WSL,             /* Windows Subsystem for Linux (WSL1 or WSL2) */
    FL_PLATFORM_LINUX,           /* Native Linux distribution or VM */
    FL_PLATFORM_MACOS,           /* macOS / Darwin */
} fl_platform_type_t;

fl_platform_type_t fl_platform_detect(void);
const char        *fl_platform_name(fl_platform_type_t t);

#endif /* FL_PLATFORM_H */

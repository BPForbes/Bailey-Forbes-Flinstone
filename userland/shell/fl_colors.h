#ifndef FL_COLORS_H
#define FL_COLORS_H

#include <stdarg.h>
#include <stdio.h>

/*
 * ANSI colour macros and tagged print helpers for the Flinstone shell.
 *
 * Palette (matches docs/SERVER.md §2 and the announcement convention in
 * contracts/networking/contract_p3_server.h):
 *
 *   RED  -> "[ERROR] <text>"            -- shell or server errors
 *   GRN  -> "[Server] <text>"           -- local success acknowledgements
 *   BLU  -> "[Server Announcement] ..." -- host/server-pushed announcements
 *
 * Helpers always append "\n" and always emit the closing RESET so a missing
 * newline in the caller's format string cannot leak the colour into the next
 * line. When stdout/stderr is not a TTY (or FL_COLORS_NO_COLOR is non-zero)
 * the ANSI sequences are suppressed and only the tagged text is written, so
 * pipes, logs, and CI capture stay readable.
 *
 * This is a header-only helper to keep the dependency surface minimal: anyone
 * that includes it gets the printers without extra Makefile churn.
 */

#define FL_COLOR_RED   "\x1B[31m"
#define FL_COLOR_GRN   "\x1B[32m"
#define FL_COLOR_YEL   "\x1B[33m"
#define FL_COLOR_BLU   "\x1B[34m"
#define FL_COLOR_MAG   "\x1B[35m"
#define FL_COLOR_CYN   "\x1B[36m"
#define FL_COLOR_WHT   "\x1B[37m"
#define FL_COLOR_RESET "\x1B[0m"

/* Global suppression hook. Tests / batch mode set this to 1 to strip ANSI. */
static int fl_color_disabled = 0;

static inline void fl_color_set_disabled(int disabled) {
    fl_color_disabled = disabled ? 1 : 0;
}

static inline int fl_color_is_enabled_for(FILE *fp) {
    if (fl_color_disabled)
        return 0;
    if (!fp)
        return 0;
#if defined(_POSIX_C_SOURCE) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__FreeBSD__)
    {
        extern int isatty(int);
        extern int fileno(FILE *);
        return isatty(fileno(fp)) ? 1 : 0;
    }
#else
    return 0;
#endif
}

static inline void fl_color_vfprintf_tagged(FILE *fp, const char *color,
                                            const char *tag, const char *fmt,
                                            va_list ap) {
    int use_color;

    if (!fp)
        fp = stdout;
    use_color = fl_color_is_enabled_for(fp);

    if (use_color)
        fputs(color, fp);
    fputs(tag, fp);
    vfprintf(fp, fmt, ap);
    if (use_color)
        fputs(FL_COLOR_RESET, fp);
    fputc('\n', fp);
    fflush(fp);
}

/* Blue, "[Server Announcement] " prefix; goes to stdout. */
static inline void fl_color_announce(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, FL_COLOR_BLU,
                             "[Server Announcement] ", fmt, ap);
    va_end(ap);
}

/* Red, "[ERROR] " prefix; goes to stderr by convention. */
static inline void fl_color_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stderr, FL_COLOR_RED, "[ERROR] ", fmt, ap);
    va_end(ap);
}

/* Green, "[Server] " prefix; goes to stdout. */
static inline void fl_color_success(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, FL_COLOR_GRN, "[Server] ", fmt, ap);
    va_end(ap);
}

/* Yellow, "[Server] " prefix for warnings; goes to stdout. */
static inline void fl_color_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, FL_COLOR_YEL, "[Server] ", fmt, ap);
    va_end(ap);
}

#endif /* FL_COLORS_H */

/* Implements fl_colors.h; the prelude mutex is released by the postlude, and hooks are NULL in tests. */

#include "fl_colors.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

int                   fl_color_disabled    = 0;
fl_color_prelude_fn   fl_color_prelude_hook  = NULL;
fl_color_postlude_fn  fl_color_postlude_hook = NULL;

void fl_color_set_disabled(int disabled) {
    fl_color_disabled = disabled ? 1 : 0;
}

int fl_color_is_enabled_for(FILE *fp) {
    if (fl_color_disabled || !fp)
        return 0;
    return isatty(fileno(fp)) ? 1 : 0;
}

void fl_color_vfprintf_tagged(FILE *fp, const char *color, const char *tag,
                              const char *fmt, va_list ap) {
    int use_color;

    if (!fp)
        fp = stdout;
    if (fl_color_prelude_hook)
        fl_color_prelude_hook();
    use_color = fl_color_is_enabled_for(fp);
    if (use_color && color)
        fputs(color, fp);
    if (tag)
        fputs(tag, fp);
    vfprintf(fp, fmt, ap);
    if (use_color)
        fputs(KNRM, fp);
    fputc('\n', fp);
    fflush(fp);
    if (fl_color_postlude_hook)
        fl_color_postlude_hook();
}

void fl_color_announce(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, KBLU, "[Server Announcement]: ", fmt, ap);
    va_end(ap);
}
void fl_color_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stderr, KRED, "[ERROR] ", fmt, ap);
    va_end(ap);
}
void fl_color_success(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, KGRN, "[Server] ", fmt, ap);
    va_end(ap);
}
void fl_color_warn_local(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stderr, KYEL, "[WARNING] ", fmt, ap);
    va_end(ap);
}
void fl_color_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, KYEL, "[Server] ", fmt, ap);
    va_end(ap);
}

/* No hooks, no newline, no tag: the postlude would redraw over the nick prompt. Foreground thread only. */
void fl_color_prompt_yellow(const char *fmt, ...) {
    va_list ap;
    int use_color = fl_color_is_enabled_for(stdout);
    if (use_color)
        fputs(KYEL, stdout);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    if (use_color)
        fputs(KNRM, stdout);
    fflush(stdout);
}


static void msg_vprint(const char *prefix, const char *fmt, va_list ap) {
    int use_color = fl_color_is_enabled_for(stdout);
    if (fl_color_prelude_hook)
        fl_color_prelude_hook();
    if (use_color)
        fputs(KCYN, stdout);
    fputs(prefix, stdout);
    vfprintf(stdout, fmt, ap);
    if (use_color)
        fputs(KNRM, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (fl_color_postlude_hook)
        fl_color_postlude_hook();
}

void fl_color_msg_to_user(const char *recipient_display, const char *fmt, ...) {
    char prefix[160];
    va_list ap;
    snprintf(prefix, sizeof(prefix), "[Server Message, You -> %s]: ",
             recipient_display ? recipient_display : "?");
    va_start(ap, fmt);
    msg_vprint(prefix, fmt, ap);
    va_end(ap);
}
void fl_color_msg_from_user(const char *sender_display, const char *fmt, ...) {
    char prefix[160];
    va_list ap;
    snprintf(prefix, sizeof(prefix), "[Server Message, %s -> You]: ",
             sender_display ? sender_display : "?");
    va_start(ap, fmt);
    msg_vprint(prefix, fmt, ap);
    va_end(ap);
}
void fl_color_msg_to_all(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    msg_vprint("[Server Message, You -> All]: ", fmt, ap);
    va_end(ap);
}
void fl_color_msg_from_all(const char *sender_display, const char *fmt, ...) {
    char prefix[160];
    va_list ap;
    snprintf(prefix, sizeof(prefix), "[Server Message, From %s]: ",
             sender_display ? sender_display : "?");
    va_start(ap, fmt);
    msg_vprint(prefix, fmt, ap);
    va_end(ap);
}

void fl_color_server_dm(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    msg_vprint("[SERVER]: ", fmt, ap);
    va_end(ap);
}

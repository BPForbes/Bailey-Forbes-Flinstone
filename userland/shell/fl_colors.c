/*
 * Implementation of the colour helpers declared in fl_colors.h.
 *
 * Lives in userland/shell/ so the shell binary, every test binary, and the
 * embedded-VM build all link a single definition of `fl_color_disabled` and
 * the prompt-aware hook pointers (no weak-symbol tricks, no header-only
 * surprises).
 *
 * Prompt-aware hook protocol:
 *   - `fl_color_prelude_hook`  is called BEFORE writing the tagged colour
 *     line. The shell registers a callback that takes a pthread mutex and,
 *     if the interactive readline is currently sitting on a "shell> ..."
 *     line, clears that line so the announcement is not glued onto it.
 *   - `fl_color_postlude_hook` is called AFTER the trailing newline. The
 *     shell callback redraws "shell> " plus whatever the user had typed,
 *     then releases the mutex.
 *
 * Tests and non-shell binaries leave the hooks at NULL; printing then just
 * writes to stdout/stderr as before.
 */

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

/* Single-line tagged helpers (newline appended). */

void fl_color_announce(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* "[Server Announcement]: " — colon after the tag, per the user spec. */
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
void fl_color_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stdout, KYEL, "[Server] ", fmt, ap);
    va_end(ap);
}

/* Yellow interactive prompt: NO newline, NO tag — caller controls layout
 * (used by the nick prompt during `server join`).
 *
 * Bypasses the prelude/postlude hooks entirely. The prelude would acquire a
 * mutex that the postlude is responsible for releasing, and we deliberately
 * skip the postlude (so the redraw of "shell> " does not paint over the
 * prompt the user is about to answer). Caller is expected to be on the
 * foreground command thread; no other thread should be writing to stdout
 * during the brief Y/N + nick-read window. */
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

/* [Server Message, ...]: family — cyan, prompt-aware. */

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

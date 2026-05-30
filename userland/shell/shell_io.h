#ifndef SHELL_IO_H
#define SHELL_IO_H

#include <stddef.h>

/*
 * Prompt-aware async print support for the interactive shell.
 *
 * Background threads (server / client receive loops) call into
 * fl_colors.h helpers that, if registered, invoke
 *   - fl_shell_io_async_prelude()  : pthread_mutex_lock + clear current line
 *   - fl_shell_io_async_postlude() : redraw "shell> " + buffered input + unlock
 *
 * The interpreter keeps the shared buffer state up to date via
 *   - fl_shell_io_set_prompt_active(active, line, len, pos)
 *
 * Tests / non-interactive binaries do not call fl_shell_io_init() and the
 * fl_colors.h hooks stay NULL — colour output then writes directly without
 * any prompt dance.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Register the prelude/postlude hooks with fl_colors.h. Idempotent. */
void fl_shell_io_init(void);

/* Called by interpreter.c around each input read.
 * `active` = 1 when the shell is currently waiting for input at "shell> ".
 * `line`, `len`, `pos` describe the in-progress input buffer so the postlude
 * can redraw exactly what the user had typed. */
void fl_shell_io_set_prompt_active(int active, const char *line, int len, int pos);

/* External callers should generally use fl_color_* helpers; these are exposed
 * for tests and for code paths that need raw locked stdout access. */
void fl_shell_io_lock(void);
void fl_shell_io_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_IO_H */

/*
 * shell_io.c -- prompt-aware async print support for the interactive shell.
 *
 * Owns one pthread mutex around stdout and a snapshot of the readline-style
 * input buffer maintained by interpreter.c. Background threads route through
 * here (via fl_colors.h hooks) so that an asynchronous announcement does not
 * appear glued to the live "shell> ..." prompt.
 */

#include "shell_io.h"

#include "fl_colors.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define FL_SHELL_IO_BUF_MAX 1024

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int  g_prompt_active = 0;
static char g_prompt_buf[FL_SHELL_IO_BUF_MAX];
static int  g_prompt_len = 0;
static int  g_prompt_pos = 0;
static int  g_inited = 0;

void fl_shell_io_lock(void)   { pthread_mutex_lock(&g_lock); }
void fl_shell_io_unlock(void) { pthread_mutex_unlock(&g_lock); }

static void prelude(void) {
    pthread_mutex_lock(&g_lock);
    if (g_prompt_active) {
        /* CR + clear-to-end-of-line so the announcement starts at column 0
         * with nothing left over from the partially-typed input. */
        fputs("\r\033[2K", stdout);
        fflush(stdout);
    }
}

static void postlude(void) {
    if (g_prompt_active) {
        /* Redraw the prompt and whatever the user had typed. */
        fputs("shell> ", stdout);
        if (g_prompt_len > 0)
            fwrite(g_prompt_buf, 1, (size_t)g_prompt_len, stdout);
        if (g_prompt_pos < g_prompt_len) {
            int back = g_prompt_len - g_prompt_pos;
            if (back > 0 && back < 10000)
                fprintf(stdout, "\033[%dD", back);
        }
        fflush(stdout);
    }
    pthread_mutex_unlock(&g_lock);
}

void fl_shell_io_init(void) {
    if (g_inited)
        return;
    fl_color_prelude_hook  = prelude;
    fl_color_postlude_hook = postlude;
    g_inited = 1;
}

void fl_shell_io_set_prompt_active(int active, const char *line, int len, int pos) {
    pthread_mutex_lock(&g_lock);
    g_prompt_active = active ? 1 : 0;
    if (!active) {
        g_prompt_len = 0;
        g_prompt_pos = 0;
        g_prompt_buf[0] = '\0';
    } else {
        if (len < 0) len = 0;
        if (len > FL_SHELL_IO_BUF_MAX - 1) len = FL_SHELL_IO_BUF_MAX - 1;
        if (line && len > 0)
            memcpy(g_prompt_buf, line, (size_t)len);
        g_prompt_buf[len] = '\0';
        g_prompt_len = len;
        if (pos < 0) pos = 0;
        if (pos > len) pos = len;
        g_prompt_pos = pos;
    }
    pthread_mutex_unlock(&g_lock);
}

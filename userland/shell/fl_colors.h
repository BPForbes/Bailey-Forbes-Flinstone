#ifndef FL_COLORS_H
#define FL_COLORS_H

#include <stdarg.h>
#include <stdio.h>

/*
 * Flinstone shell colour helpers.
 *
 * ANSI macros (K-prefixed, matching the convention in contracts/networking/
 * contract_p3_server.h and docs/SERVER.md). Source pattern from
 * https://stackoverflow.com/a/3586005 (karlphillip et al., CC BY-SA 2.5,
 * retrieved 2026-05-29):
 *
 *   #define KNRM  "\x1B[0m"
 *   #define KRED  "\x1B[31m"
 *   #define KGRN  "\x1B[32m"
 *   #define KYEL  "\x1B[33m"
 *   #define KBLU  "\x1B[34m"
 *   #define KMAG  "\x1B[35m"
 *   #define KCYN  "\x1B[36m"
 *   #define KWHT  "\x1B[37m"
 *
 * Palette:
 *   KRED  -> "[ERROR] <text>"                  -- shell or server errors
 *   KGRN  -> "[Server] <text>"                 -- local success acks
 *   KYEL  -> warnings, interactive nick prompt
 *   KBLU  -> "[Server Announcement]: <text>"   -- host/server-pushed announces
 *   KCYN  -> "[Server Message, ...]: <text>"   -- public / private chat
 *
 * Prompt-aware print: each tagged helper calls into optional shell-side
 * prelude/postlude hooks so that asynchronous output (background receive
 * loop) does not glue onto the shell's currently-displayed `shell> ` prompt.
 * The hooks are NULL by default (and tests / non-shell binaries simply skip
 * them); the shell registers a pthread-mutexed implementation in
 * `userland/shell/shell_io.c` that clears the input line, prints the tagged
 * message on its own line, then redraws `shell> ` plus the typed buffer.
 */

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

/* Back-compat aliases (older code may still reference FL_COLOR_*). */
#define FL_COLOR_RED   KRED
#define FL_COLOR_GRN   KGRN
#define FL_COLOR_YEL   KYEL
#define FL_COLOR_BLU   KBLU
#define FL_COLOR_MAG   KMAG
#define FL_COLOR_CYN   KCYN
#define FL_COLOR_WHT   KWHT
#define FL_COLOR_RESET KNRM

#ifdef __cplusplus
extern "C" {
#endif

/* Global colour-disable flag (set to 1 by tests/pipes to strip ANSI). */
extern int fl_color_disabled;

/* Prompt-aware hooks. Both NULL by default. Registered by shell_io_init(). */
typedef void (*fl_color_prelude_fn)(void);   /* called before tagged line */
typedef void (*fl_color_postlude_fn)(void);  /* called after newline */
extern fl_color_prelude_fn fl_color_prelude_hook;
extern fl_color_postlude_fn fl_color_postlude_hook;

void fl_color_set_disabled(int disabled);
int  fl_color_is_enabled_for(FILE *fp);

void fl_color_vfprintf_tagged(FILE *fp, const char *color, const char *tag,
                              const char *fmt, va_list ap);

/* Blue, "[Server Announcement] " prefix (stdout). */
void fl_color_announce(const char *fmt, ...);
/* Red, "[ERROR] " prefix (stderr). */
void fl_color_error(const char *fmt, ...);
/* Green, "[Server] " prefix (stdout). */
void fl_color_success(const char *fmt, ...);
/* Yellow, "[Server] " prefix (stdout); also used for the interactive nick prompt. */
void fl_color_warn(const char *fmt, ...);
/* Yellow, no tag — for interactive prompts (no trailing newline). */
void fl_color_prompt_yellow(const char *fmt, ...);

/* Cyan "[Server Message, ...]: " family for public and private chat lines. */
void fl_color_msg_to_user(const char *recipient_display, const char *fmt, ...);
void fl_color_msg_from_user(const char *sender_display, const char *fmt, ...);
void fl_color_msg_to_all(const char *fmt, ...);
void fl_color_msg_from_all(const char *sender_display, const char *fmt, ...);

/*
 * Cyan "[SERVER]: <text>" -- system-to-individual server message (e.g.
 * the host-transfer notification "You are now the host."). Distinct from
 * the `[Server Message, ...]` chat family above: this one is the server
 * itself talking to one user, not a chat between peers.
 */
void fl_color_server_dm(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* FL_COLORS_H */

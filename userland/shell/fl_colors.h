#ifndef FL_COLORS_H
#define FL_COLORS_H

#include <stdarg.h>
#include <stdio.h>

/* ANSI colour macros and tagged print helpers; prelude and postlude hooks wrap each tagged line. */

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

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

extern int fl_color_disabled;

/* Prompt-aware hooks: NULL by default, registered by shell_io_init(). */
typedef void (*fl_color_prelude_fn)(void);
typedef void (*fl_color_postlude_fn)(void);
extern fl_color_prelude_fn fl_color_prelude_hook;
extern fl_color_postlude_fn fl_color_postlude_hook;

void fl_color_set_disabled(int disabled);
int  fl_color_is_enabled_for(FILE *fp);

void fl_color_vfprintf_tagged(FILE *fp, const char *color, const char *tag,
                              const char *fmt, va_list ap);

void fl_color_announce(const char *fmt, ...);
void fl_color_error(const char *fmt, ...);
void fl_color_success(const char *fmt, ...);
void fl_color_warn(const char *fmt, ...);
void fl_color_warn_local(const char *fmt, ...);
void fl_color_prompt_yellow(const char *fmt, ...);

void fl_color_msg_to_user(const char *recipient_display, const char *fmt, ...);
void fl_color_msg_from_user(const char *sender_display, const char *fmt, ...);
void fl_color_msg_to_all(const char *fmt, ...);
void fl_color_msg_from_all(const char *sender_display, const char *fmt, ...);

void fl_color_server_dm(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* FL_COLORS_H */

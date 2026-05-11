#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

#define RESOLVE_PATH_BUF 256

char *trim_whitespace(char *str);
void resolve_path(const char *path, char *out, size_t outsize);
void append_history(const char *cmd);
char *read_history_line(int index);
char **load_history(int *count);
void free_history(char **history, int count);

/* Clears embedded shell history on the current volume (FLINT tail or legacy #SHELL lines). */
int delete_history_storage(void);

#endif /* UTIL_H */

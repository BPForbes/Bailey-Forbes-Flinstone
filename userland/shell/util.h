#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdio.h>

#define RESOLVE_PATH_BUF 256
#define HISTORY_STAGING_PATH_SZ 512

char *trim_whitespace(char *str);
void resolve_path(const char *path, char *out, size_t outsize);
void append_history(const char *cmd);
char *read_history_line(int index);
char **load_history(int *count);
void free_history(char **history, int count);

/* Open command history for reading. When tmp_staging[0] is non-NUL after success, fclose then unlink(tmp_staging). */
FILE *history_fopen_read(char tmp_staging[HISTORY_STAGING_PATH_SZ]);
/* Remove persisted history (FAT32 volume file and/or legacy host file). Returns 0 on success, -1 on error. */
int delete_history_storage(void);

#endif /* UTIL_H */

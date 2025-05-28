#ifndef UTIL_H
#define UTIL_H

char *trim_whitespace(char *str);
void append_history(const char *cmd);
char *read_history_line(int index);
char **load_history(int *count);
void free_history(char **history, int count);

#endif /* UTIL_H */

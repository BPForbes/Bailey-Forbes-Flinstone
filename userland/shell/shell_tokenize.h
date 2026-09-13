#ifndef USERLAND_SHELL_SHELL_TOKENIZE_H
#define USERLAND_SHELL_SHELL_TOKENIZE_H

#include <stddef.h>

/**
 * Split a mutable command line into argv[] (like strtok, but honors "..." and '...').
 * Modifies line in place. Returns argc (argv[argc] is set to NULL when argv_cap > 0).
 */
int fl_shell_tokenize_line(char *line, char **argv, int argv_cap);

/** Remove one layer of surrounding " or ' quotes from s (in place). */
void fl_shell_strip_outer_quotes(char *s);

#endif /* USERLAND_SHELL_SHELL_TOKENIZE_H */

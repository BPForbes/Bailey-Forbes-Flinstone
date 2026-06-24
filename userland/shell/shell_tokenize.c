#include "shell_tokenize.h"

#include <string.h>

void fl_shell_strip_outer_quotes(char *s)
{
	size_t n;

	if (!s)
		return;
	n = strlen(s);
	if (n >= 2u &&
	    ((s[0] == '"' && s[n - 1u] == '"') || (s[0] == '\'' && s[n - 1u] == '\''))) {
		memmove(s, s + 1, n - 2u);
		s[n - 2u] = '\0';
	}
}

int fl_shell_tokenize_line(char *line, char **argv, int argv_cap)
{
	char *p;
	int argc = 0;

	if (!line || !argv || argv_cap < 1)
		return 0;

	p = line;
	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		if (argc >= argv_cap - 1)
			break;

		if (*p == '"') {
			char *dst;

			p++;
			argv[argc++] = p;
			dst = p;
			while (*p) {
				if (*p == '\\' && p[1]) {
					p++;
					*dst++ = *p++;
				} else if (*p == '"') {
					*dst = '\0';
					p++;
					break;
				} else {
					*dst++ = *p++;
				}
			}
			*dst = '\0';
			continue;
		}
		if (*p == '\'') {
			char *dst;

			p++;
			argv[argc++] = p;
			dst = p;
			while (*p && *p != '\'')
				*dst++ = *p++;
			*dst = '\0';
			if (*p == '\'')
				p++;
			continue;
		}

		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p) {
			*p = '\0';
			p++;
		}
	}

	argv[argc] = NULL;
	return argc;
}

#ifndef CMD_UTIL_H
#define CMD_UTIL_H

/* VM jail check shared by builtins (see cmd_util.c). */
int cmd_jail_blocked_path(const char *op, const char *input, const char *resolved);

#endif /* CMD_UTIL_H */

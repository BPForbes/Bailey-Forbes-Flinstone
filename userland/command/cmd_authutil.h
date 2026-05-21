#ifndef CMD_AUTHUTIL_H
#define CMD_AUTHUTIL_H

#include <stddef.h>

/** Read a password on the next line (no echo). Returns 0 on success. */
int cmd_read_password(const char *prompt, char *buf, size_t buf_size);

/** Zero-fill a password buffer. */
void cmd_wipe_password(char *buf, size_t buf_size);

/**
 * Non-elevated users must verify their own password before sudo elevation.
 * Elevated accounts succeed immediately. Returns 0 on success.
 */
int cmd_sudo_require_auth(void);

/** Like cmd_sudo_require_auth; uses reason for elevation grant and audit. */
int cmd_sudo_require_auth_reason(const char *reason);

/**
 * Verify target user's password (for su). Always prompts on the next line.
 * Returns 0 on success.
 */
int cmd_su_require_auth(const char *target);

#endif /* CMD_AUTHUTIL_H */

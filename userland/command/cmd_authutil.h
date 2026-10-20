#ifndef CMD_AUTHUTIL_H
#define CMD_AUTHUTIL_H

#include "contract_result.h"

#include <stddef.h>

/** Read a password on the next line (no echo). Returns 0 on success. */
int cmd_read_password(const char *prompt, char *buf, size_t buf_size);

/** Wipe a password buffer (asm_mem_secure_zero). */
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

/**
 * Prompts once for the logged-in account's password ("Password (<why>): ") and opens its
 * signing key into signing and, when box is non-NULL, its box key into box (32 bytes each;
 * either may be NULL). Keys the account lacks are created first and saved; a failed save
 * undoes exactly what was created. Prints nothing: the caller reports the result.
 * FL_RESULT_OK, FL_RESULT_EOF (password not read), FL_RESULT_ACCES (wrong password) or
 * another error. The caller wipes the secrets.
 */
fl_result_t cmd_open_account_keys(const char *why, unsigned char *signing, unsigned char *box);

#endif /* CMD_AUTHUTIL_H */

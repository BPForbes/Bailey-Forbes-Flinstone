#ifndef CMD_AUTHUTIL_H
#define CMD_AUTHUTIL_H

#include "contract_result.h"

#include <stddef.h>

/** Read a password on the next line (no echo). Returns 0 on success. */
int cmd_read_password(const char *prompt, char *buf, size_t buf_size);

/** Wipe a password buffer (asm_mem_secure_zero). */
void cmd_wipe_password(char *buf, size_t buf_size);

/**
 * Non-elevated users give Root's password and their own (the sudo co-signature) before elevation.
 * Elevated accounts succeed immediately. Returns 0 on success.
 */
int cmd_sudo_require_auth(void);

/** Like cmd_sudo_require_auth; uses reason for elevation grant and audit. */
int cmd_sudo_require_auth_reason(const char *reason);
/** Signs a sudo command line and restarts the window; asks for the full co-signature again when the
 *  window cannot be renewed. Returns 0 when signed, 1 when sudo cannot go on. */
int cmd_sudo_sign_action(const char *line);
/** Signs a command run inside the window without restarting it. When Root's key is unavailable the
 *  elevation ends and the command runs as the account itself. */
void cmd_sudo_sign_elevated(const char *line);

/**
 * Runs su or sudo -i (sudo non-zero): prompts for the factor KeyQuorum demands and hands it to
 * fl_session_switch_user. Every refusal prints the same message; every password is wiped.
 * Returns 0 when the session is now target.
 */
int cmd_switch_user(const char *cmd, const char *target, int sudo);

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

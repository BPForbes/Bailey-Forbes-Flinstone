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
/** Sign a sudo command line inside the window and start its clock again (FL-20 #399). When Root's key is
 *  not available the elevation has ended: sudo asks for Root's key again and signs the line under the new
 *  window. 0 when signed, 1 when sudo cannot go on. */
int cmd_sudo_sign_action(const char *line);
/** Sign a command run inside the window (not a sudo: the clock is not reset). When Root's key is not
 *  available the elevation ends and the command runs as the account itself. */
void cmd_sudo_sign_elevated(const char *line);

/**
 * su and sudo -i (sudo non-zero): asks KeyQuorum which factor the switch to target demands
 * (fl_kq_unlock_requirement), prompts for it (the target's password, or Root's for sudo -i; for a
 * bridge, once that password is verified, also a bridge member's password: the caller when a member,
 * else the account it names, refused when over-long) and hands it to fl_session_switch_user, which
 * asks again, checks and commits. Every refusal prints the same "<cmd>: authentication failed"; the
 * reason is only in the audit. Every password is wiped. Returns 0 when the session is now target.
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

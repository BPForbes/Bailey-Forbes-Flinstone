#ifndef CMD_AUTHUTIL_H
#define CMD_AUTHUTIL_H

#include "contract_result.h"

#include <stddef.h>

int cmd_read_password(const char *prompt, char *buf, size_t buf_size);

void cmd_wipe_password(char *buf, size_t buf_size);

int cmd_sudo_require_auth(void);

int cmd_sudo_require_auth_reason(const char *reason);
int cmd_sudo_sign_action(const char *line);
void cmd_sudo_sign_elevated(const char *line);

int cmd_switch_user(const char *cmd, const char *target, int sudo);

fl_result_t cmd_open_account_keys(const char *why, unsigned char *signing, unsigned char *box);

#endif /* CMD_AUTHUTIL_H */

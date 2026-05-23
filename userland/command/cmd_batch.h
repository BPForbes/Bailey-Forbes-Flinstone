#ifndef CMD_BATCH_H
#define CMD_BATCH_H

#include "fl_shell_cmd.h"

/**
 * Batch argv grouping: how many contiguous tokens starting at **i** form one shell
 * invocation. Used by **sh.c** batch mode and **sudo** layout validation (P7).
 */
int fl_batch_argv_tokens_count(int argc, char **argv, int i);

/** True when **argv[1..argc)** parses as whole-batch layout with **sudo** at sudo_i..sudo_end. */
int fl_batch_argv_layout_valid(int argc, char **argv, int sudo_i, int sudo_end);

/** Per-command batch arity (mirrors **fl_shell_cmd_dispatch**). */
int fl_shell_cmd_batch_tokens_count(fl_shell_cmd_no_t no, int argc, char **argv, int i);

/** Optional path argument: 2 when argv[i+1] exists and is not a flag, else 1. */
int cmd_batch_opt_path_arity(int argc, char **argv, int i);

/** Optional -y/-Y or -n/-N pair after command name. */
int cmd_batch_flag_yn_arity(int argc, char **argv, int i);

/** Optional -y/-Y only. */
int cmd_batch_flag_y_arity(int argc, char **argv, int i);

/** True when **tok** names a registered or legacy batch shell command. */
int cmd_batch_token_is_shell_command(const char *tok);

#endif /* CMD_BATCH_H */

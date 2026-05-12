#ifndef SHELL_HISTORY_ASM_H
#define SHELL_HISTORY_ASM_H

#include <stddef.h>

/* Append cmd[0..cmd_len) and LF at buf+used; return new length or (size_t)-1. GAS: shell_history_host_asm.s; also audit_log.c staging. */
size_t history_asm_append_record(char *buf, size_t cap, size_t used,
                                 const char *cmd, size_t cmd_len);

#endif /* SHELL_HISTORY_ASM_H */

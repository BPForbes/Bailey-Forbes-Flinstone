#ifndef SHELL_HISTORY_ASM_H
#define SHELL_HISTORY_ASM_H

#include <stddef.h>

/*
 * Append cmd[0 .. cmd_len) and a newline to buf at offset `used`.
 * Returns the new logical length (used + cmd_len + 1), or (size_t)-1 if
 * cap - used is too small. Implemented in arch (GAS) shell_history_host_asm.s
 * for x86_64 and AArch64 host builds; NASM libc-only builds use a C fallback in util.c.
 *
 * Also used by **audit_log.c** to stage one audit line (text without embedded LF,
 * plus trailing LF) before `fwrite`, matching the same bounded-copy contract.
 */
size_t history_asm_append_record(char *buf, size_t cap, size_t used,
                                   const char *cmd, size_t cmd_len);

#endif /* SHELL_HISTORY_ASM_H */

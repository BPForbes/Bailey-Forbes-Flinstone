/*
 * Minimal stub for cmd_sudo_batch_tokens_count tests (issue #220 / #204).
 * Avoids linking cmd_batch.o and the full dispatch table.
 */
#include "cmd_batch.h"
#include <string.h>

int fl_batch_argv_layout_valid(int argc, char **argv, int sudo_i, int sudo_end) {
    if (sudo_i < 1 || sudo_i >= argc || !argv[sudo_i] || strcmp(argv[sudo_i], "sudo"))
        return 0;
    if (sudo_end <= sudo_i || sudo_end > argc)
        return 0;
    return 1;
}

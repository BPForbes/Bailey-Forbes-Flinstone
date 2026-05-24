#include "cmd_batch.h"
#include "cmd_decl.h"
#include <string.h>

int cmd_batch_opt_path_arity(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-')
        return 2;
    return 1;
}

int cmd_batch_flag_yn_arity(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] &&
        (!strcmp(argv[i + 1], "-y") || !strcmp(argv[i + 1], "-n") ||
         !strcmp(argv[i + 1], "-Y") || !strcmp(argv[i + 1], "-N")))
        return 2;
    return 1;
}

int cmd_batch_flag_y_arity(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] &&
        (!strcmp(argv[i + 1], "-y") || !strcmp(argv[i + 1], "-Y")))
        return 2;
    return 1;
}

int cmd_batch_token_is_shell_command(const char *tok) {
    if (!tok || !tok[0])
        return 0;
    if (fl_shell_cmd_lookup(tok) != FL_SCMD_UNKNOWN)
        return 1;
    if (!strcmp(tok, "help") || !strcmp(tok, "clear") || !strcmp(tok, "history") ||
        !strcmp(tok, "his") || !strcmp(tok, "cc") || !strcmp(tok, "exit") ||
        !strcmp(tok, "bios") || !strcmp(tok, "make") || !strcmp(tok, "type"))
        return 1;
    return 0;
}

#if !defined(TEST_BATCH_ARGV_HELPERS_ONLY)

static int batch_unknown_tokens_count(int argc, char **argv, int i) {
    int j = i + 1;

    while (j < argc && argv[j] && argv[j][0] != '-' && strcmp(argv[j], "make") != 0 &&
           strcmp(argv[j], "write") != 0)
        j++;
    return j - i;
}

int fl_batch_argv_layout_valid(int argc, char **argv, int sudo_i, int sudo_end) {
    int cur = 1;

    if (sudo_i < 1 || sudo_end <= sudo_i || sudo_end > argc)
        return 0;
    while (cur < argc) {
        int tc;

        if (cur == sudo_i)
            tc = sudo_end - sudo_i;
        else
            tc = fl_batch_argv_tokens_count(argc, argv, cur);
        if (tc < 1)
            return 0;
        cur += tc;
    }
    return 1;
}

int fl_batch_argv_tokens_count(int argc, char **argv, int i) {
    const char *cmd;
    fl_shell_cmd_no_t no;

    if (i >= argc || !argv[i])
        return 0;
    cmd = argv[i];
    no = fl_shell_cmd_lookup(cmd);
    if (no != FL_SCMD_UNKNOWN)
        return fl_shell_cmd_batch_tokens_count(no, argc, argv, i);

    if (!strcmp(cmd, "help"))
        return cmd_help_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "clear"))
        return cmd_clear_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "history") || !strcmp(cmd, "his"))
        return cmd_history_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "cc"))
        return cmd_cc_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "exit"))
        return cmd_exit_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "bios"))
        return cmd_bios_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "make"))
        return cmd_make_batch_tokens_count(argc, argv, i);
    if (!strcmp(cmd, "type"))
        return cmd_cat_batch_tokens_count(argc, argv, i);

    return batch_unknown_tokens_count(argc, argv, i);
}

#endif /* !TEST_BATCH_ARGV_HELPERS_ONLY */

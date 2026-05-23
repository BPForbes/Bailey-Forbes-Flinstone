#include "cmd_decl.h"
#include "contract_p7_shell_batch.h"
#include <ctype.h>
#include <string.h>

_Static_assert(FL_CONTRACT_P7_BATCH_AUDIT_SHOW_MAX_TOKENS == 3u,
               "cmd_audit_batch_tokens_count must match P7 contract cap");

static int batch_audit_show_has_count_token(const char *tok) {
    const char *p;

    if (!tok || !tok[0])
        return 0;
    p = tok;
    if (*p == '+' || *p == '-') {
        if (p[1] == '\0')
            return 0;
        p++;
    }
    if (!isdigit((unsigned char)*p))
        return 0;
    for (p++; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    return 1;
}

int cmd_audit_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc && !strcmp(argv[i + 1], "show")) {
        if (i + 2 < argc && batch_audit_show_has_count_token(argv[i + 2]))
            return 3;
        return 2;
    }
    if (i + 1 < argc &&
        (!strcmp(argv[i + 1], "path") || !strcmp(argv[i + 1], "ring") ||
         !strcmp(argv[i + 1], "--help") || !strcmp(argv[i + 1], "-h")))
        return 2;
    return 1;
}

int fl_batch_audit_tokens_count(int argc, char **argv, int i) {
    return cmd_audit_batch_tokens_count(argc, argv, i);
}

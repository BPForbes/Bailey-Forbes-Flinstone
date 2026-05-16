#include "fl/batch_argv.h"
#include <ctype.h>
#include <string.h>

static int audit_show_has_count_token(const char *tok) {
    if (!tok || !tok[0])
        return 0;
    if (isdigit((unsigned char)tok[0]))
        return 1;
    if ((tok[0] == '-' || tok[0] == '+') && tok[1] != '\0' &&
        isdigit((unsigned char)tok[1]))
        return 1;
    return 0;
}

int fl_batch_audit_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc && !strcmp(argv[i + 1], "show")) {
        if (i + 2 < argc && audit_show_has_count_token(argv[i + 2]))
            return 3;
        return 2;
    }
    if (i + 1 < argc &&
        (!strcmp(argv[i + 1], "path") || !strcmp(argv[i + 1], "ring") ||
         !strcmp(argv[i + 1], "--help") || !strcmp(argv[i + 1], "-h")))
        return 2;
    return 1;
}

int fl_batch_contracts_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc &&
        (!strcmp(argv[i + 1], "summary") || !strcmp(argv[i + 1], "json") ||
         !strcmp(argv[i + 1], "--help") || !strcmp(argv[i + 1], "-h")))
        return 2;
    return 1;
}

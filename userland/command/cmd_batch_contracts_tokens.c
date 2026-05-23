#include "cmd_decl.h"
#include "contract_p7_shell_batch.h"
#include <string.h>

_Static_assert(FL_CONTRACT_P7_BATCH_CONTRACTS_QUALIFIED_MAX_TOKENS == 2u,
               "cmd_contracts_batch_tokens_count must match P7 contract cap");

int cmd_contracts_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc &&
        (!strcmp(argv[i + 1], "summary") || !strcmp(argv[i + 1], "json") ||
         !strcmp(argv[i + 1], "--help") || !strcmp(argv[i + 1], "-h")))
        return 2;
    (void)argc;
    return 1;
}

int fl_batch_contracts_tokens_count(int argc, char **argv, int i) {
    return cmd_contracts_batch_tokens_count(argc, argv, i);
}

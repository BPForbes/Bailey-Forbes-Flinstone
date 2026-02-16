#include "fs_chain.h"
#include <stdlib.h>

void fs_chain_init(fs_validation_chain_t *chain) {
    chain->head = NULL;
}

void fs_chain_add(fs_validation_chain_t *chain, fs_validator_fn fn) {
    fs_chain_node_t *n = malloc(sizeof(*n));
    if (!n) return;
    n->validate = fn;
    n->next = chain->head;
    chain->head = n;
}

int fs_chain_run(fs_validation_chain_t *chain, const char *path, const char *arg2, void *ctx) {
    for (fs_chain_node_t *n = chain->head; n; n = n->next) {
        int r = n->validate(path, arg2, ctx);
        if (r != 0)
            return r;
    }
    return 0;
}

void fs_chain_clear(fs_validation_chain_t *chain) {
    fs_chain_node_t *n = chain->head;
    while (n) {
        fs_chain_node_t *next = n->next;
        free(n);
        n = next;
    }
    chain->head = NULL;
}

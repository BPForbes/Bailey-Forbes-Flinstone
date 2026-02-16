#ifndef FS_CHAIN_H
#define FS_CHAIN_H

#include "fs_types.h"

/* Chain of Responsibility: validation before operations */
typedef int (*fs_validator_fn)(const char *path, const char *arg2, void *ctx);

typedef struct fs_chain_node {
    fs_validator_fn validate;
    struct fs_chain_node *next;
} fs_chain_node_t;

typedef struct {
    fs_chain_node_t *head;
} fs_validation_chain_t;

void fs_chain_init(fs_validation_chain_t *chain);
void fs_chain_add(fs_validation_chain_t *chain, fs_validator_fn fn);
int fs_chain_run(fs_validation_chain_t *chain, const char *path, const char *arg2, void *ctx);
void fs_chain_clear(fs_validation_chain_t *chain);

#endif /* FS_CHAIN_H */

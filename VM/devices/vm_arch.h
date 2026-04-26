#ifndef VM_ARCH_H
#define VM_ARCH_H

#include <stdio.h>

struct vm_host;

#define VM_LAYER_COUNT 5
#define VM_ARCH_CHECKPOINTS 11

typedef struct vm_arch_state {
    int layer_ready[VM_LAYER_COUNT];
    int checkpoints_ready[VM_ARCH_CHECKPOINTS];
} vm_arch_state_t;

enum {
    VM_LAYER_CORE = 0,
    VM_LAYER_DRIVER_MODEL = 1,
    VM_LAYER_DEVICE_BOUNDARY = 2,
    VM_LAYER_SERVICES = 3,
    VM_LAYER_RUNTIME = 4
};

void vm_arch_collect(const struct vm_host *host, vm_arch_state_t *out);
int vm_arch_layers_ready(const vm_arch_state_t *state);
int vm_arch_checkpoints_ready(const vm_arch_state_t *state);
void vm_arch_report(FILE *out, const vm_arch_state_t *state);

#endif /* VM_ARCH_H */

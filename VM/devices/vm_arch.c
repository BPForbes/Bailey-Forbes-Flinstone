#include "vm_arch.h"
#include "vm_host.h"
#include "vm_disk.h"
#include "vm_io.h"
#include "vm_loader.h"
#include "vm_snapshot.h"
#include "drivers.h"

#include <string.h>

void vm_arch_collect(vm_host_t *host, vm_arch_state_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));

    /*
     * VM adaptation of:
     * - "Add layered driver model slice (#21)" => 5 layers.
     * - "Improve system architecture for bare metal (#24)" => 11 checkpoints.
     */
    out->checkpoints_ready[0]  = (host && host->mem.ram && host->mem.size > 0);  /* core host state */
    out->checkpoints_ready[1]  = (g_block_driver != NULL);                        /* block path present */
    out->checkpoints_ready[2]  = (g_keyboard_driver != NULL);                     /* keyboard path present */
    out->checkpoints_ready[3]  = (g_display_driver != NULL);                      /* display path present */
    out->checkpoints_ready[4]  = (g_timer_driver != NULL);                        /* timer path present */
    out->checkpoints_ready[5]  = (g_pic_driver != NULL);                          /* irq/pic path present */
    out->checkpoints_ready[6]  = vm_disk_is_active();                             /* VM disk boundary active */
    out->checkpoints_ready[7]  = vm_io_pci_ready();                               /* virtual PCI config allocated */
    out->checkpoints_ready[8]  = vm_io_reset_line_ready();                        /* reset path initialized */
    out->checkpoints_ready[9]  = vm_io_syscall_bridge_ready() && vm_io_serial_ready(); /* userspace bridge + serial */
    out->checkpoints_ready[10] = (VM_BOOT_ENTRY_CS == 0x07c0 && VM_BOOT_ENTRY_IP == 0); /* deterministic entry */

    out->layer_ready[VM_LAYER_CORE] = out->checkpoints_ready[0];
    out->layer_ready[VM_LAYER_DRIVER_MODEL] =
        out->checkpoints_ready[1] && out->checkpoints_ready[2] &&
        out->checkpoints_ready[3] && out->checkpoints_ready[4] &&
        out->checkpoints_ready[5];
    out->layer_ready[VM_LAYER_DEVICE_BOUNDARY] =
        out->checkpoints_ready[6] && out->checkpoints_ready[7] && out->checkpoints_ready[8];
    out->layer_ready[VM_LAYER_SERVICES] =
        out->checkpoints_ready[9];
    out->layer_ready[VM_LAYER_RUNTIME] = out->checkpoints_ready[10];
}

int vm_arch_layers_ready(const vm_arch_state_t *state) {
    if (!state) return 0;
    for (int i = 0; i < VM_LAYER_COUNT; i++) {
        if (!state->layer_ready[i]) return 0;
    }
    return 1;
}

int vm_arch_checkpoints_ready(const vm_arch_state_t *state) {
    if (!state) return 0;
    for (int i = 0; i < VM_ARCH_CHECKPOINTS; i++) {
        if (!state->checkpoints_ready[i]) return 0;
    }
    return 1;
}

void vm_arch_report(FILE *out, const vm_arch_state_t *state) {
    if (!out || !state) return;
    fprintf(out, "[VM] 5-layer driver system: L0=%d L1=%d L2=%d L3=%d L4=%d\n",
            state->layer_ready[0], state->layer_ready[1], state->layer_ready[2],
            state->layer_ready[3], state->layer_ready[4]);
    fprintf(out, "[VM] 11-point architecture improvements:");
    for (int i = 0; i < VM_ARCH_CHECKPOINTS; i++)
        fprintf(out, " %d", state->checkpoints_ready[i]);
    fputc('\n', out);
}

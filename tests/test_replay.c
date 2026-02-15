/* Replay invariant: run N -> checkpoint -> run M -> restore -> run M = identical state */
#ifdef VM_ENABLE

#include "vm.h"
#include "common.h"
#include "fs_service_glue.h"
#include "path_log.h"
#include "drivers/drivers.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    fs_service_glue_init();
    path_log_init();
    /* Skip drivers_init: VM uses vm_disk directly; display/timer have fallbacks */

    if (vm_boot() != 0) {
        fprintf(stderr, "vm_boot failed\n");
        fs_service_glue_shutdown();
        return 1;
    }

    vm_save_checkpoint();

    vm_run_cycles(10);
    vm_save_checkpoint();

    vm_run_cycles(5);
    uint32_t after_15 = vm_state_checksum();

    vm_restore_checkpoint();
    vm_run_cycles(5);
    uint32_t after_15_replay = vm_state_checksum();

    vm_stop();
    fs_service_glue_shutdown();

    if (after_15 != after_15_replay) {
        fprintf(stderr, "replay mismatch: %08X vs %08X\n", after_15, after_15_replay);
        return 1;
    }
    printf("replay invariant OK\n");
    return 0;
}

#else

int main(void) {
    fprintf(stderr, "VM_ENABLE not set, skip replay test\n");
    return 0;
}

#endif

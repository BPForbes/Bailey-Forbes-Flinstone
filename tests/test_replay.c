/* Replay invariant: run N -> checkpoint -> run M -> restore -> run M = identical state */
#ifdef VM_ENABLE

#include "vm.h"
#include "common.h"
#include "fs_service_glue.h"
#include "path_log.h"
#include "drivers/drivers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/fl_replay_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        close(fd);
        FILE *fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "XX:00000000000000000000000000000000\n");
            fprintf(fp, "00:00000000000000000000000000000000\n");
            fclose(fp);
        }
        strncpy(current_disk_file, path, sizeof(current_disk_file) - 1);
        current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    }
    g_total_clusters = 8;
    g_cluster_size = 32;
    fs_service_glue_init();
    path_log_init();
    drivers_init(path[0] ? path : NULL);

    if (vm_boot() != 0) {
        fprintf(stderr, "vm_boot failed\n");
        drivers_shutdown();
        if (path[0]) unlink(path);
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
    drivers_shutdown();
    if (path[0]) unlink(path);
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

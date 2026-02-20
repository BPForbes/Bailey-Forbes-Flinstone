/**
 * Boot init - phased startup with [ OK ] / [ FAIL ] logging.
 */
#include "boot_init.h"
#include "fs_service_glue.h"
#include "path_log.h"
#include "drivers/drivers.h"
#include "common.h"
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#ifdef VM_ENABLE
#include "vm.h"
#endif

static const char *s_boot_disk_file;

static int boot_phase(const char *name, int (*fn)(void)) {
    printf("        %s... ", name);
    fflush(stdout);
    if (fn() == 0) {
        printf("[ OK ]\n");
        return 0;
    }
    printf("[ FAIL ]\n");
    return -1;
}

static int phase_fs_service(void) {
    fs_service_glue_init();
    return (g_fm_service != NULL) ? 0 : -1;
}

static int phase_path_log(void) {
    path_log_init();
    return 0;
}

static int phase_drivers(void) {
    drivers_init(s_boot_disk_file ? s_boot_disk_file : current_disk_file);
    return 0;  /* drivers_init exits on fail; reaching here = success */
}

#ifdef VM_ENABLE
static int phase_vm_boot(void) {
    return vm_boot();
}
#endif

static int phase_scheduler(void) {
    signal(SIGINT, SIG_IGN);
    pq_init(&g_pool.pq);
    g_pool.shutting_down = 0;
    pthread_mutex_init(&g_pool.mutex, NULL);
    pthread_cond_init(&g_pool.cond, NULL);
#ifndef BATCH_SINGLE_THREAD
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&g_pool.workers[i], NULL, worker_thread, NULL);
    }
#endif
    return 0;
}

int boot_run(const char *disk_file, int for_vm) {
    s_boot_disk_file = disk_file;
    (void)for_vm;  /* used only when VM_ENABLE */
    printf("\n  Flinstone Boot\n");
    printf("  -------------\n");

    if (boot_phase("File system service", phase_fs_service) != 0)
        return -1;
    if (boot_phase("Path log", phase_path_log) != 0)
        return -1;
    if (boot_phase("Drivers", phase_drivers) != 0)
        return -1;

#ifdef VM_ENABLE
    if (for_vm) {
        if (boot_phase("VM", phase_vm_boot) != 0)
            return -1;
    } else
#endif
    {
        if (boot_phase("Scheduler", phase_scheduler) != 0)
            return -1;
    }

    printf("  -------------\n");
    printf("  System ready.\n\n");
    return 0;
}

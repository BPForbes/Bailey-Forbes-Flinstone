/******************************************************************************
 * BPForbes_Flinstone_Shell – Single-Session Thread-Pool Shell (Interactive &
 *                              Batch Modes)
 * ----------------------------------------------------------------------------
 * Author: Bailey Forbes
 * CSCI P436 Projects: P03, P05, P07, P08, & P09
 *
 * Description:
 *   This shell supports both interactive and batch modes. Disk operations are
 *   performed on text files and managed in clusters. Clusters are displayed in
 *   hexadecimal. The new –cd command (or its batch shortcut) creates a new disk
 *   file (<volume>_disk.dat legacy hex, or <volume>_disk.img for FAT32) filled with random data, then prints its contents.
 *
 *   The –cd command syntax is:
 *       -cd <volume> <rowCount> <nibbleCount> [<interactive>]
 *   The batch shortcut (invoked with exactly 3 or 4 parameters and the first not
 *   starting with a dash) is equivalent. In both cases the optional parameter is
 *   either -y or -n; if omitted in batch mode, –n is assumed.
 *
 *   Additionally, the –v and exit commands in batch mode assume –n if no flag is given.
 *
 * Compilation:
 *   Use the provided Makefile. To compile the main shell program, run:
 *
 *       make
 *
 *   This builds the executable "BPForbes_Flinstone_Shell".
 *
 *   To compile the unit tests (which use the CUnit testing framework), run:
 *
 *       make BPForbes_Flinstone_Tests
 *
 *   To install CUnit on Debian/Ubuntu, you can run:
 *
 *       sudo apt install libcunit1 libcunit1-dev
 *
 *   Unit tests are defined in BPForbes_Flinstone_Tests.c.
 *
 * Usage:
 *   - Interactive mode: simply run "./BPForbes_Flinstone_Shell" to enter the shell.
 *   - Batch mode: supply commands as arguments (e.g., "./BPForbes_Flinstone_Shell -v").
 *
 *****************************************************************************/

#include "common.h"
#include "shell_io.h"
#include <stdint.h>
#include "threadpool.h"
#include "interpreter.h"
#include "terminal.h"
#include "disk.h"
#include "fs_service_glue.h"
#include "path_log.h"
#include "drivers/drivers.h"
#include "fs_jail.h"
#include "fl/session.h"
#include "fl/authz_subsystem.h"
#include "net_netdev.h"
#include "VM/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <limits.h>
#include "util.h"
#include "cmd_batch.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Recursively remove directory and contents */
static void rmrf(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        char sub[PATH_MAX];
        snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode))
            rmrf(sub);
        else
            unlink(sub);
    }
    closedir(d);
    rmdir(path);
}

static void vm_cleanup_at_exit(void) {
    if (!g_vm_mode || !g_vm_cleanup || !g_vm_root[0]) return;
    if (chdir("/tmp") != 0) return;
    rmrf(g_vm_root);
}

static void shell_netdev_cleanup_at_exit(void) {
    fl_net_netdev_shutdown();
}

#ifndef BATCH_SINGLE_THREAD
static int g_pool_workers_started;
#endif
static int g_pool_cleanup_done;

static void shell_pool_cleanup(void) {
    if (g_pool_cleanup_done)
        return;
    g_pool_cleanup_done = 1;
#ifndef BATCH_SINGLE_THREAD
    if (g_pool_workers_started) {
        pthread_mutex_lock(&g_pool.mutex);
        g_pool.shutting_down = 1;
        pthread_cond_broadcast(&g_pool.cond);
        pthread_mutex_unlock(&g_pool.mutex);
        for (int i = 0; i < NUM_WORKERS; i++)
            pthread_join(g_pool.workers[i], NULL);
    }
#endif
    pthread_mutex_destroy(&g_pool.mutex);
    pthread_cond_destroy(&g_pool.cond);
}

/* Case-insensitive compare (no locale/allocation) */
static int eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Strip -Virtualization and -y/-n from argv; set g_vm_mode. Returns new argc. */
static int parse_vm_args(int argc, char *argv[]) {
    int out = 1;  /* argv[0] always kept */
    int vm_confirm = 0;  /* -y = cleanup on exit */
    for (int i = 1; i < argc; i++) {
        if (eq_ci(argv[i], "-Virtualization")) {
            g_vm_mode = 1;
            continue;
        }
        if (eq_ci(argv[i], "-y") && g_vm_mode) {
            vm_confirm = 1;
            continue;
        }
        if (eq_ci(argv[i], "-n") && g_vm_mode) {
            continue;
        }
        if (eq_ci(argv[i], "-vm") && g_vm_mode) {
            g_vm_run_embedded = 1;
            continue;
        }
        argv[out++] = argv[i];
    }
    argv[out] = NULL;
    if (g_vm_mode)
        g_vm_cleanup = vm_confirm;
    return out;
}

/* Spawn popup terminal running shell in VM sandbox. Returns 0 on success, -1 if no terminal. */
static int vm_spawn_popup(const char *exe_path) {
    char cmd[PATH_MAX + 128];
    snprintf(cmd, sizeof(cmd), "cd '%s' && exec '%s'", g_vm_root, exe_path);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *xterm_argv[] = { "xterm", "-title", "Flinstone VM", "-e", "sh", "-c", cmd, NULL };
        execvp("xterm", xterm_argv);
        char *gnome_argv[] = { "gnome-terminal", "--", "sh", "-c", cmd, NULL };
        execvp("gnome-terminal", gnome_argv);
        char *konsole_argv[] = { "konsole", "-e", "sh", "-c", cmd, NULL };
        execvp("konsole", konsole_argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WEXITSTATUS(status) == 127)
        return -1;
    return 0;
}

static int vm_configure_root_from_cwd(void) {
    if (!g_vm_mode || g_vm_root[0])
        return 0;
    char root[PATH_MAX];
    if (!getcwd(root, sizeof(root)))
        return -1;
    strncpy(g_vm_root, root, CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    return 0;
}

static void vm_warn_layer_config(void) {
    if (!g_vm_mode)
        return;
    int warned = 0;
    if (!g_cwd[0] || !current_disk_file[0]) {
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 0 core/common is not configured\n");
        warned = 1;
    }
    if (!g_block_driver || !g_keyboard_driver || !g_display_driver || !g_timer_driver || !g_pic_driver) {
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 1 disk/drivers is not configured\n");
        warned = 1;
    }
    if (!fs_jail_root_configured()) {
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 2 filesystem sandbox root is not configured\n");
        warned = 1;
    }
    if (!fs_service_glue_is_ready() || !path_log_is_initialized()) {
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 3 services/path logging is not configured\n");
        warned = 1;
    }
    if (!g_vm_root[0]) {
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 4 shell/VM root is not configured\n");
        warned = 1;
    }
    (void)warned;
}

int main(int argc, char *argv[]) {
    /* Lab weak seeds are opt-in; set FL_USERS_LAB_DEFAULTS=1 to enable. */
    if (!getenv("FL_USERS_LAB_DEFAULTS"))
        (void)setenv("FL_USERS_LAB_DEFAULTS", "0", 0);
    /* Register the prompt-aware async print hooks so server / client
     * background threads can interleave colour-tagged output with the
     * interactive readline without gluing onto "shell> ". */
    fl_shell_io_init();
    /* Seed the random number generator */
    srand((unsigned) time(NULL));

    /* Parse -Virtualization -y first */
    argc = parse_vm_args(argc, argv);

    /* Fast path: single commands that need no init (no allocation) */
    if (argc == 2 && argv[1]) {
        if (strcmp(argv[1], "help") == 0) {
            fl_print_help_message();
            exit(0);
        }
        if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "-v") == 0) {
            printf("BPForbes_Flinstone_Shell v%s\n", VERSION_LINE);
            exit(0);
        }
        if (strcmp(argv[1], "clear") == 0) {
            printf("\033c");
            exit(0);
        }
    }

    /* VM popup: -Virtualization -y with no other args (no -vm) -> spawn popup once */
    if (g_vm_mode && g_vm_cleanup && argc == 1 && !g_vm_run_embedded) {
        char tmpl[] = "/tmp/flintstone_vm_XXXXXX";
        char *root = mkdtemp(tmpl);
        if (!root) {
            fprintf(stderr, "VM: failed to create temp directory\n");
            exit(1);
        }
        strncpy(g_vm_root, root, CWD_MAX - 1);
        g_vm_root[CWD_MAX - 1] = '\0';
        char exe_path[PATH_MAX];
        exe_path[0] = '\0';
#ifdef __linux__
        ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (n > 0) exe_path[n] = '\0';
#endif
        if (!exe_path[0] && argv[0] && argv[0][0] == '/') {
            strncpy(exe_path, argv[0], sizeof(exe_path) - 1);
            exe_path[sizeof(exe_path) - 1] = '\0';
        }
        if (!exe_path[0])
            snprintf(exe_path, sizeof(exe_path), "./%s", "BPForbes_Flinstone_Shell");
        printf("[VM] Sandbox: %s\n", g_vm_root);
        printf("[VM] Opening VM window...\n");
        if (vm_spawn_popup(exe_path) != 0) {
            fprintf(stderr, "VM: no terminal found (install xterm, gnome-terminal, or konsole).\n");
            fprintf(stderr, "VM: Run in current terminal: cd '%s' && exec '%s'\n", g_vm_root, exe_path);
        }
        if (chdir("/tmp") == 0)
            rmrf(g_vm_root);
        exit(0);
    }

    /* Initialize current working directory */
    if (getcwd(g_cwd, sizeof(g_cwd)) == NULL)
        g_cwd[0] = '.', g_cwd[1] = '\0';

    /* VM command mode uses a temp sandbox; embedded VM runs from the launch root. */
    if (g_vm_mode && argc > 1 && !g_vm_run_embedded) {
        char tmpl[] = "/tmp/flintstone_vm_XXXXXX";
        char *root = mkdtemp(tmpl);
        if (!root) {
            fprintf(stderr, "VM: failed to create temp directory\n");
            exit(1);
        }
        strncpy(g_vm_root, root, CWD_MAX - 1);
        g_vm_root[CWD_MAX - 1] = '\0';
        if (chdir(g_vm_root) != 0) {
            fprintf(stderr, "VM: failed to chdir to %s\n", g_vm_root);
            rmdir(g_vm_root);
            exit(1);
        }
        strncpy(g_cwd, g_vm_root, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = '\0';
        printf("[VM] Sandbox: %s\n", g_vm_root);
        if (g_vm_cleanup)
            atexit(vm_cleanup_at_exit);
    }

    /* Batch shortcut: if exactly 4 or 5 parameters and argv[1] is not a known command,
       treat as createdisk shortcut.
    */
    if ((argc == 4 || argc == 5) && argv[1] && argv[1][0] != '-') {
        /* Must include every builtin name so batch mode does not treat them as createdisk shortcut */
        static const char *skip[] = {"help","cd","dir","make","write","cat","type","mkdir","rmdir",
            "rmtree","mv","version","contracts","audit","exit","bios","clear","history","his","cc","listclusters","listdirs",
            "setdisk","createdisk","format","search","writecluster","delcluster","update","redirect",
            "initdisk","rerun","import","du","printdisk","addcluster","where","loc",
            "diskput","diskget","diskfiles","diskdel","diskmkdir","sudo","su","login",
            "logout","useradd","userdel","passwd","whoami","ping","check","server",
            "udpsend","udplisten",NULL};
        int is_cmd = 0;
        for (int k = 0; skip[k]; k++)
            if (!strcmp(argv[1], skip[k])) { is_cmd = 1; break; }
        if (!is_cmd) {
        int rowCount = atoi(argv[2]);
        int nibbleCount = atoi(argv[3]);
        if (rowCount <= 0 || nibbleCount <= 0 || (nibbleCount % 2 != 0)) {
            fprintf(stderr, "Error: rowCount must be positive and nibbleCount must be positive and even.\n");
            exit(1);
        }
        flintstone_format_disk(argv[1], rowCount, nibbleCount);
        int cb = nibbleCount / 2;
        if (cb >= 512 && (cb % 512) == 0)
            snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.img", argv[1]);
        else
            snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.dat", argv[1]);
        read_disk_header();
        print_disk_formatted();
        if (argc == 5 && !strcmp(argv[4], "-y"))
            interactive_shell();
        exit(0);
        }
    }

    /* No args: help and exit, unless -Virtualization -y -vm (then run shell after guest VM) */
    if (argc < 2 && !(g_vm_mode && g_vm_run_embedded)) {
        fl_print_help_message();
        exit(0);
    }

    /* VM mode: confine host file I/O to vm_hostfs/ under the launch directory (or temp VM sandbox). */
    if (vm_configure_root_from_cwd() != 0)
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 4 shell/VM root is not configured\n");
    fs_jail_init();
    fl_session_init();
    fl_net_netdev_init();
    fl_net_netdev_set_authz_hook(fl_authz_subsystem_check, NULL);
    atexit(shell_netdev_cleanup_at_exit);

    /* Default host volume: ensure drive.img exists before block driver probes it. */
    if (strcmp(current_disk_file, "drive.img") == 0) {
        if (access(current_disk_file, F_OK) != 0) {
            disk_ensure_default_fat32(current_disk_file, 32, 512);
        }
        read_disk_header();
    }

    /* Initialize file manager service, path log, and drivers */
    fs_service_glue_init();
    path_log_init();
    drivers_init(NULL);
    vm_warn_layer_config();

    /* Initialize thread pool and signals */
    signal(SIGINT, SIG_IGN);
    pq_init(&g_pool.pq);
    g_pool.shutting_down = 0;
    if (pthread_mutex_init(&g_pool.mutex, NULL) != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(errno));
        exit(1);
    }
    if (pthread_cond_init(&g_pool.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_pool.mutex);
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(errno));
        exit(1);
    }
#ifndef BATCH_SINGLE_THREAD
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&g_pool.workers[i], NULL, worker_thread, NULL) != 0) {
            pthread_mutex_lock(&g_pool.mutex);
            g_pool.shutting_down = 1;
            pthread_cond_broadcast(&g_pool.cond);
            pthread_mutex_unlock(&g_pool.mutex);
            for (int j = 0; j < i; j++)
                pthread_join(g_pool.workers[j], NULL);
            pthread_cond_destroy(&g_pool.cond);
            pthread_mutex_destroy(&g_pool.mutex);
            fprintf(stderr, "pthread_create: %s\n", strerror(errno));
            exit(1);
        }
    }
    g_pool_workers_started = 1;
#endif
    atexit(shell_pool_cleanup);
    if (original_stdout_fd < 0) {
        original_stdout_fd = dup(fileno(stdout));
        original_stdout_file = fdopen(original_stdout_fd, "w");
    }

    /* Embedded x86 guest first, then same session continues to interactive shell */
    if (g_vm_mode && g_vm_run_embedded && argc == 1) {
        const char *vlog = getenv("VM_LOG_LEVEL");
        int lvl = vlog ? atoi(vlog) : 1;
        if (lvl >= 1) printf("[VM] Booting embedded VM...\n");
        if (vm_boot() == 0) {
            vm_run();
            vm_stop();
            if (lvl >= 1) printf("[VM] Halted.\n");
        } else {
            fprintf(stderr, "[VM] Boot failed.\n");
#ifndef VM_ENABLE
            fprintf(stderr, "[VM] This binary was not built with the embedded VM. Run: make vm   (then use this executable)\n");
#endif
        }
    }

    /* Process batch-mode arguments first */
    if (argc > 1) {
        int i = 1;
        while (i < argc) {
            int tokensCount;
            char *cmd = argv[i];
            if (!cmd) { i++; continue; }
            tokensCount = fl_batch_argv_tokens_count(argc, argv, i);
            if (!strcmp(cmd, "exit") && tokensCount == 1) {
                submit_single_command("exit -n");
                i++;
                continue;
            }
            if (!strcmp(cmd, "setdisk") || !strcmp(cmd, "createdisk")) {
                const int minTokens = (!strcmp(cmd, "setdisk")) ? 2 : 4;

                if (tokensCount > 0 && tokensCount < minTokens) {
                    fprintf(stderr, "batch: %s: insufficient arguments (skipped)\n",
                            cmd);
                    i += tokensCount;
                    continue;
                }
            }
            if (tokensCount > argc - i)
                tokensCount = argc - i;
            if (tokensCount <= 0) { i++; continue; }
            size_t totalLen = 0;
            int overflow = 0;
            for (int k = i; k < i + tokensCount; k++) {
                size_t sl = strlen(argv[k]);
                size_t add = sl + 1;
                if (totalLen > SIZE_MAX - add) {
                    overflow = 1;
                    break;
                }
                totalLen += add;
            }
            if (overflow || totalLen > SIZE_MAX - 1) {
                fprintf(stderr, "batch: command length overflow, skipping tokens\n");
                i += tokensCount;
                continue;
            }
            char *commandStr = malloc(totalLen + 1);
            if (!commandStr) { i += tokensCount; continue; }
            commandStr[0] = '\0';
            for (int k = i; k < i + tokensCount; k++) {
                strcat(commandStr, argv[k]);
                if (k < i + tokensCount - 1)
                    strcat(commandStr, " ");
            }
            submit_single_command(commandStr);
            free(commandStr);
            i += tokensCount;
        }
    }
    if (!isatty(STDIN_FILENO))
        exit(0);
    else
        interactive_shell();

    shell_pool_cleanup();
    drivers_shutdown();
    path_log_shutdown();
    fs_service_glue_shutdown();
    return 0;
}

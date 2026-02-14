/******************************************************************************
 * BPForbes_Flinstone_Shell – Single-Session Thread-Pool Shell (Interactive &
 *                              Batch Modes)
 * ----------------------------------------------------------------------------
 * Date: 03/07/25
 * Author: Bailey Forbes
 * CSCI P436 Projects: P03, P05, P07, P08, & P09
 *
 * Description:
 *   This shell supports both interactive and batch modes. Disk operations are
 *   performed on text files and managed in clusters. Clusters are displayed in
 *   hexadecimal. The new –cd command (or its batch shortcut) creates a new disk
 *   file (<volume>_disk.txt) filled with random data, then prints its contents.
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
#include "threadpool.h"
#include "interpreter.h"
#include "terminal.h"
#include "disk.h"
#include "fs_service_glue.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    /* Seed the random number generator */
    srand((unsigned) time(NULL));

    /* Batch shortcut: if exactly 3 or 4 parameters and the first parameter does not
       begin with a dash, treat it as a batch shortcut equivalent to the -cd command.
    */
    if ((argc == 4 || argc == 5) && argv[1][0] != '-') {
        int rowCount = atoi(argv[2]);
        int nibbleCount = atoi(argv[3]);
        if (rowCount <= 0 || nibbleCount <= 0 || (nibbleCount % 2 != 0)) {
            fprintf(stderr, "Error: rowCount must be positive and nibbleCount must be positive and even.\n");
            exit(1);
        }
        flintstone_format_disk(argv[1], rowCount, nibbleCount);
        snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.txt", argv[1]);
        g_cluster_size = nibbleCount / 2;
        g_total_clusters = rowCount;
        print_disk_formatted();
        if (argc == 5 && !strcmp(argv[4], "-y"))
            interactive_shell();
        exit(0);
    }

    /* If no command-line arguments are provided, print the help message and exit */
    if (argc < 2) {
        printf("%s\n", HELP_MSG);
        exit(0);
    }

    /* Initialize file manager service (facade + providers) */
    fs_service_glue_init();

    /* Initialize thread pool and signals */
    signal(SIGINT, SIG_IGN);
    g_pool.head = 0;
    g_pool.tail = 0;
    g_pool.shutting_down = 0;
    pthread_mutex_init(&g_pool.mutex, NULL);
    pthread_cond_init(&g_pool.cond, NULL);
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&g_pool.workers[i], NULL, worker_thread, NULL);
    }
    if (original_stdout_fd < 0) {
        original_stdout_fd = dup(fileno(stdout));
        original_stdout_file = fdopen(original_stdout_fd, "w");
    }

    /* Process batch-mode arguments first */
    if (argc > 1) {
        int i = 1;
        while (i < argc) {
            int tokensCount = 1;
            char *cmd = argv[i];
            if (!strcmp(cmd, "-?") || !strcmp(cmd, "-h") || !strcmp(cmd, "help") ||
                !strcmp(cmd, "-l") || !strcmp(cmd, "-M") || !strcmp(cmd, "clear") ||
                !strcmp(cmd, "history") || !strcmp(cmd, "his") || !strcmp(cmd, "cc"))
            {
                tokensCount = 1;
            }
            else if (!strcmp(cmd, "-v") || !strcmp(cmd, "-V")) {
                if (i + 1 < argc &&
                    (!strcmp(argv[i + 1], "-y") || !strcmp(argv[i + 1], "-n") ||
                     !strcmp(argv[i + 1], "-Y") || !strcmp(argv[i + 1], "-N"))) {
                    tokensCount = 2;
                } else {
                    tokensCount = 1;
                }
            }
            else if (!strcmp(cmd, "exit")) {
                if (i + 1 < argc &&
                    (!strcmp(argv[i+1], "-y") || !strcmp(argv[i+1], "-Y") ||
                     !strcmp(argv[i+1], "-n") || !strcmp(argv[i+1], "-N"))) {
                    tokensCount = 2;
                } else {
                    submit_single_command("exit -n");
                    i++;
                    continue;
                }
            }
            else if (!strcmp(cmd, "-f") || !strcmp(cmd, "-cd"))
                tokensCount = ((argc > i+3) ? ((argc > i+4) ? 5 : 4) : 0);
            else if (!strcmp(cmd, "-F"))
                tokensCount = 5;
            else if (!strcmp(cmd, "dir")) {
                if (i + 1 < argc && argv[i+1][0] != '-')
                    tokensCount = 2;
                else
                    tokensCount = 1;
            }
            else if (!strcmp(cmd, "make"))
                tokensCount = 2;
            else if (!strcmp(cmd, "-s") || !strcmp(cmd, "-C") ||
                     !strcmp(cmd, "-d") || !strcmp(cmd, "-uc") || !strcmp(cmd, "-O"))
                tokensCount = 2;
            else if (!strcmp(cmd, "-w"))
                tokensCount = 4;
            else if (!strcmp(cmd, "-init"))
                tokensCount = 3;
            else if (!strcmp(cmd, "-import")) {
                if (i + 4 < argc)
                    tokensCount = 5;
                else
                    tokensCount = 3;
            }
            else if (!strcmp(cmd, "-up"))
                tokensCount = 4;
            else if (!strcmp(cmd, "sc")) {
                if (i + 2 < argc && (!strcmp(argv[i+1], "-t") || !strcmp(argv[i+1], "-h")))
                    tokensCount = 3;
                else
                    tokensCount = 1;
            }
            else {
                int j = i + 1;
                while (j < argc && argv[j][0] != '-' && strcmp(argv[j], "make") != 0)
                    j++;
                tokensCount = j - i;
            }
            int totalLen = 0;
            for (int k = i; k < i + tokensCount; k++)
                totalLen += (int)strlen(argv[k]) + 1;
            char *commandStr = malloc(totalLen + 1);
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
    
    if (isatty(STDIN_FILENO) && strcmp(current_disk_file, "drive.txt") == 0) {
        FILE *testfp = fopen(current_disk_file, "r");
        if (!testfp) {
            printf("No default disk file '%s'. Creating fresh with 32 clusters of %d bytes each.\n",
                   current_disk_file, g_cluster_size);
            FILE *fp = fopen(current_disk_file, "w");
            if (fp) {
                char *ruler = malloc(g_cluster_size * 2 + 1);
                if (ruler) {
                    const char *digits = "0123456789ABCDEF";
                    for (int j = 0; j < g_cluster_size * 2; j++)
                        ruler[j] = digits[j % 16];
                    ruler[g_cluster_size * 2] = '\0';
                    fprintf(fp, "XX:%s\n", ruler);
                    free(ruler);
                }
                for (int i = 0; i < 32; i++) {
                    fprintf(fp, "%02X:", i);
                    for (int j = 0; j < g_cluster_size * 2; j++)
                        fputc('0', fp);
                    fputc('\n', fp);
                }
                fclose(fp);
            }
        } else {
            fclose(testfp);
            read_disk_header();
        }
    }
    
    if (!isatty(STDIN_FILENO))
        exit(0);
    else
        interactive_shell();
    
    pthread_mutex_lock(&g_pool.mutex);
    g_pool.shutting_down = 1;
    pthread_cond_broadcast(&g_pool.cond);
    pthread_mutex_unlock(&g_pool.mutex);
    for (int i = 0; i < NUM_WORKERS; i++)
        pthread_join(g_pool.workers[i], NULL);
    pthread_mutex_destroy(&g_pool.mutex);
    pthread_cond_destroy(&g_pool.cond);
    fs_service_glue_shutdown();
    return 0;
}

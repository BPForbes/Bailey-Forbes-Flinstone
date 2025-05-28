#include "interpreter.h"
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "disk.h"
#include "cluster.h"
#include "fs.h"         /* Provides list_files() and list_directories() */
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

/* 
 * execute_command_str:
 *   Parses and executes the command provided in 'line'.
 *   Returns 0 on success; otherwise a nonzero value.
 */
int execute_command_str(const char *line) {
    if (!line || !*line)
        return 0;
    append_history(line);
    char buffer[512];
    strncpy(buffer, line, sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = '\0';
    char *tokenBuf = strdup(buffer);
    char *trimmed = trim_whitespace(tokenBuf);

    /* Process "exit" command */
    if (!strncmp(trimmed, "exit", 4)) {
        char *tokens[3] = { NULL, NULL, NULL };
        int tcount = 0;
        char *tok = strtok(trimmed, " \t");
        while (tok && tcount < 3) {
            tokens[tcount++] = tok;
            tok = strtok(NULL, " \t");
        }
        if (tcount >= 2) {
            if (!strcmp(tokens[1], "-y") || !strcmp(tokens[1], "-Y")) {
                if (unlink(HISTORY_FILE) == 0)
                    printf("History file deleted.\n");
                else
                    perror("Failed to delete history file");
            } else if (!strcmp(tokens[1], "-n") || !strcmp(tokens[1], "-N")) {
                printf("History file retained.\n");
            } else {
                printf("Usage: exit [ -y | -n ]\n");
                free(tokenBuf);
                return 1;
            }
            printf("Exiting shell...\n");
            free(tokenBuf);
            shell_running = 0;
            exit(0);
        } else {
            if (!isatty(STDIN_FILENO))
                printf("History file retained.\n");
            else {
                char response[10];
                printf("Do you want to delete your shell history? [y/n]: ");
                fflush(stdout);
                if (fgets(response, sizeof(response), stdin)) {
                    response[strcspn(response, "\n")] = '\0';
                    if (!strcasecmp(response, "y")) {
                        if (unlink(HISTORY_FILE) == 0)
                            printf("History file deleted.\n");
                        else
                            perror("Failed to delete history file");
                    } else {
                        printf("History file retained.\n");
                    }
                }
            }
            printf("Exiting shell...\n");
            free(tokenBuf);
            shell_running = 0;
            exit(0);
        }
    }
    if (!strcmp(trimmed, "clear")) {
        printf("\033c");
        free(tokenBuf);
        return 0;
    }
    if (!strcmp(trimmed, "-?") || !strcmp(trimmed, "-h") || !strcmp(trimmed, "help")) {
        printf("%s\n", HELP_MSG);
        free(tokenBuf);
        return 0;
    }
    if (!strcmp(trimmed, "history") || !strcmp(trimmed, "his")) {
        pthread_mutex_lock(&history_mutex);
        FILE *hf = fopen(HISTORY_FILE, "r");
        if (!hf) {
            pthread_mutex_unlock(&history_mutex);
            printf("No history.\n");
            free(tokenBuf);
            return 0;
        }
        char l2[256];
        int idx = 1;
        while (fgets(l2, sizeof(l2), hf))
            printf("[%d] %s", idx++, l2);
        fclose(hf);
        pthread_mutex_unlock(&history_mutex);
        free(tokenBuf);
        return 0;
    }
    if (!strcmp(trimmed, "cc")) {
        remove(HISTORY_FILE);
        printf("History cleared.\n");
        g_history_cleared = 1;
        free(tokenBuf);
        return 0;
    }
    if (!strncmp(trimmed, "make ", 5)) {
        do_make_file(trimmed + 5);
        free(tokenBuf);
        return 0;
    }
    char *cmdLine = strdup(buffer);
    char *args[64];
    int argc = 0;
    char *t = strtok(cmdLine, " \t");
    while (t && argc < 63) {
        args[argc++] = t;
        t = strtok(NULL, " \t");
    }
    args[argc] = NULL;
    free(tokenBuf);
    if (argc == 0) {
        free(cmdLine);
        return 0;
    }

    /* Command dispatch */
    if (!strcmp(args[0], "-cd")) {
        if (argc < 4) {
            printf("Usage: -cd <volume> <rowCount> <nibbleCount> [<interactive>]\n");
            free(cmdLine);
            return 1;
        }
        int rowCount = atoi(args[2]);
        int nibbleCount = atoi(args[3]);
        if (rowCount <= 0 || nibbleCount <= 0 || (nibbleCount % 2 != 0)) {
            printf("Error: rowCount must be positive and nibbleCount must be positive and even.\n");
            free(cmdLine);
            return 1;
        }
        flintstone_format_disk(args[1], rowCount, nibbleCount);
        snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.txt", args[1]);
        g_cluster_size = nibbleCount / 2;
        g_total_clusters = rowCount;
        print_disk_formatted();
        if (argc >= 5 && !strcmp(args[4], "-y"))
            interactive_shell();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-F")) {
        if (argc < 5) {
            printf("Usage: -F <disk_file> <volume> <rowCount> <nibbleCount>\n");
            free(cmdLine);
            return 1;
        }
        int rowCount = atoi(args[3]);
        int nibbleCount = atoi(args[4]);
        if (rowCount <= 0 || nibbleCount <= 0 || (nibbleCount % 2 != 0)) {
            printf("Error: rowCount must be positive and nibbleCount must be positive and even.\n");
            free(cmdLine);
            return 1;
        }
        format_disk_file(args[1], args[2], rowCount, nibbleCount);
        strncpy(current_disk_file, args[1], sizeof(current_disk_file)-1);
        current_disk_file[sizeof(current_disk_file)-1] = '\0';
        g_cluster_size = nibbleCount / 2;
        g_total_clusters = rowCount;
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-f")) {
        if (argc < 2) {
            printf("Usage: -f <disk_file>\n");
            free(cmdLine);
            return 1;
        }
        FILE *fp = fopen(args[1], "r");
        if (!fp) {
            perror("Error opening disk file");
            free(cmdLine);
            return 1;
        }
        fclose(fp);
        strncpy(current_disk_file, args[1], sizeof(current_disk_file)-1);
        current_disk_file[sizeof(current_disk_file)-1] = '\0';
        read_disk_header();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-v") || !strcmp(args[0], "-V")) {
        if (argc >= 2) {
            if (!strcmp(args[1], "-y") || !strcmp(args[1], "-Y")) {
                if (unlink(HISTORY_FILE) == 0)
                    printf("History file deleted.\n");
                else
                    perror("Failed to delete history file");
            } else if (!strcmp(args[1], "-n") || !strcmp(args[1], "-N")) {
                printf("History file retained.\n");
            } else {
                printf("Usage: %s [ -y | -n ]\n", args[0]);
                free(cmdLine);
                return 1;
            }
            printf("Shell version: %s\n", VERSION);
            free(cmdLine);
            exit(0);
        } else {
            if (!isatty(STDIN_FILENO))
                printf("History file retained.\n");
            else {
                char response[10];
                printf("Shell version: %s\n", VERSION);
                printf("Do you want to delete your shell history? [y/n]: ");
                fflush(stdout);
                if (fgets(response, sizeof(response), stdin)) {
                    response[strcspn(response, "\n")] = '\0';
                    if (!strcasecmp(response, "y")) {
                        if (unlink(HISTORY_FILE) == 0)
                            printf("History file deleted.\n");
                        else
                            perror("Failed to delete history file");
                    } else {
                        printf("History file retained.\n");
                    }
                }
            }
            free(cmdLine);
            exit(0);
        }
    } else if (!strcmp(args[0], "-l")) {
        list_clusters_contents();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-L")) {
        /* List local directories */
        list_directories();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-s")) {
        int searchMode = 0;
        char *searchStr = NULL;
        if (argc >= 3 && (!strcmp(args[2], "-h") || !strcmp(args[2], "-t"))) {
            searchStr = args[1];
            searchMode = (!strcmp(args[2], "-h")) ? 1 : 0;
        } else if (argc >= 2) {
            searchStr = args[1];
        } else {
            printf("Usage: -s <searchtext> [ -t|-h ]\n");
            free(cmdLine);
            return 1;
        }
        FILE *fp = fopen(current_disk_file, "r");
        if (!fp) {
            printf("No disk file.\n");
            free(cmdLine);
            return 0;
        }
        char linebuf[512];
        int found = 0;
        if (fgets(linebuf, sizeof(linebuf), fp) == NULL) {
            fclose(fp);
            printf("Disk file is empty.\n");
            free(cmdLine);
            return 0;
        }
        while (fgets(linebuf, sizeof(linebuf), fp)) {
            char *trim = trim_whitespace(linebuf);
            if (!*trim)
                continue;
            if (!strncmp(trim, "XX:", 3))
                continue;
            char *colon = strchr(trim, ':');
            if (!colon)
                continue;
            char *hexData = trim_whitespace(colon + 1);
            if (searchMode == 1) {
                if (strstr(hexData, searchStr)) {
                    printf("Found '%s' in sector %s: %s\n", searchStr, trim, hexData);
                    found = 1;
                }
            } else {
                char *ascii = convert_hex_to_ascii(hexData, g_cluster_size);
                if (ascii && strstr(ascii, searchStr)) {
                    printf("Found '%s' in sector %s: %s\n", searchStr, trim, ascii);
                    found = 1;
                }
                free(ascii);
            }
        }
        fclose(fp);
        if (!found)
            printf("'%s' not found.\n", searchStr);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-w")) {
        if (argc < 4) {
            printf("Usage: -w <cluster_index> <-t|-h> <data>\n");
            free(cmdLine);
            return 1;
        }
        int clu = atoi(args[1]);
        int inputIsText = (!strcmp(args[2], "-t")) ? 1 : 0;
        process_write_cluster(clu, args[3], inputIsText);
        calculate_storage_breakdown_for_cluster(clu);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-d")) {
        if (argc < 2) {
            printf("Usage: -d <cluster_index>\n");
            free(cmdLine);
            return 1;
        }
        int clu = atoi(args[1]);
        delete_cluster(clu);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-up")) {
        if (argc < 4) {
            printf("Usage: -up <cluster_index> <-t|-h> <data>\n");
            free(cmdLine);
            return 1;
        }
        int clu = atoi(args[1]);
        if (clu < 0 || clu >= g_total_clusters) {
            printf("Cluster out of range [0..%d].\n", g_total_clusters - 1);
            free(cmdLine);
            return 1;
        }
        delete_cluster(clu);
        int inputIsText = (!strcmp(args[2], "-t")) ? 1 : 0;
        process_write_cluster(clu, args[3], inputIsText);
        printf("Cluster %d updated.\n", clu);
        calculate_storage_breakdown_for_cluster(clu);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-type")) {
        /* New command to print file contents (like DOS 'type' or Unix 'cat')
           Expected usage: -type <filename> -f <disk_file>
        */
        if (argc != 4 || strcmp(args[2], "-f") != 0) {
            printf("Usage: -type <filename> -f <disk_file>\n");
            free(cmdLine);
            return 1;
        }
        strncpy(current_disk_file, args[3], sizeof(current_disk_file)-1);
        current_disk_file[sizeof(current_disk_file)-1] = '\0';
        read_disk_header();
        cat_file(args[1]);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-O")) {
        if (argc < 2) {
            printf("Usage: -O <filename> or \"-O off\"\n");
            free(cmdLine);
            return 1;
        }
        do_redirect_output(args[1]);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-init")) {
        if (argc < 3) {
            printf("Usage: -init <cluster_count> <cluster_size>\n");
            free(cmdLine);
            return 1;
        }
        int newCount = atoi(args[1]);
        int newSize = atoi(args[2]);
        if (newCount <= 0 || newSize <= 0 || newCount > 65535 || newSize > 65535) {
            printf("Invalid geometry.\n");
            free(cmdLine);
            return 1;
        }
        g_total_clusters = newCount;
        g_cluster_size = newSize;
        printf("Soft init: clusters=%d, size=%d bytes (in memory only).\n", g_total_clusters, g_cluster_size);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-uc")) {
        if (argc < 2) {
            printf("Usage: -uc <N>\n");
            free(cmdLine);
            return 1;
        }
        int idx = atoi(args[1]);
        if (idx <= 0) {
            printf("Index must be > 0.\n");
            free(cmdLine);
            return 1;
        }
        char *cmd = read_history_line(idx);
        if (!cmd) {
            printf("No such history line.\n");
            free(cmdLine);
            return 1;
        }
        printf("Re-executing command #%d: %s\n", idx, cmd);
        submit_single_command(cmd);
        free(cmd);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "-import")) {
        if (argc == 3) {
            import_text_drive(args[1], args[2], -1, -1);
            free(cmdLine);
            return 0;
        } else if (argc == 5) {
            int count = atoi(args[3]);
            int size = atoi(args[4]);
            if (count <= 0 || count > 65535 || size <= 0 || size > 65535) {
                printf("Invalid geometry for import.\n");
                free(cmdLine);
                return 1;
            }
            import_text_drive(args[1], args[2], count, size);
            free(cmdLine);
            return 0;
        } else {
            printf("Usage: -import <textfile> <txtfile> [clusters clusterSize]\n");
            free(cmdLine);
            return 1;
        }
    } else if (!strcmp(args[0], "-du")) {
        /* Modified disk usage command.
           Without additional parameter, it prints a short disk usage report.
           With the -dtl parameter it prints detailed information for specified clusters.
        */
        if (argc >= 2 && !strcmp(args[1], "-dtl")) {
            if (argc > 2) {
                for (int i = 2; i < argc; i++) {
                    int c = atoi(args[i]);
                    show_disk_detail_for_cluster(c);
                    printf("\n\n");
                }
            } else {
                for (int i = 0; i < g_total_clusters; i++)
                    show_disk_detail_for_cluster(i);
                printf("\n\n");
            }
            free(cmdLine);
            return 0;
        } else {
            FILE *fp = fopen(current_disk_file, "r");
            if (!fp) {
                printf("No disk file.\n");
                free(cmdLine);
                return 1;
            }
            char linebuf[512];
            if (fgets(linebuf, sizeof(linebuf), fp) == NULL) {
                printf("Disk file is empty.\n");
                fclose(fp);
                free(cmdLine);
                return 1;
            }
            int total = 0, used = 0, avail = 0, bad = 0;
            while (fgets(linebuf, sizeof(linebuf), fp)) {
                linebuf[strcspn(linebuf, "\n")] = '\0';
                char *colon = strchr(linebuf, ':');
                if (!colon) {
                    bad++;
                    total++;
                    continue;
                }
                char *hexdata = colon + 1;
                while (isspace(*hexdata)) hexdata++;
                size_t expected_len = g_cluster_size * 2;
                if (strlen(hexdata) != expected_len) {
                    bad++;
                    total++;
                    continue;
                }
                int all_zero = 1;
                for (size_t i = 0; i < expected_len; i++) {
                    if (hexdata[i] != '0') {
                        all_zero = 0;
                        break;
                    }
                }
                if (all_zero)
                    avail++;
                else
                    used++;
                total++;
            }
            fclose(fp);
            int used_percent = (total > 0) ? (used * 100) / total : 0;
            int avail_percent = (total > 0) ? (avail * 100) / total : 0;
            int bad_percent = (total > 0) ? (bad * 100) / total : 0;
            printf("Disk usage::\n");
            printf("State Count Percent\n");
            printf("Used %d %d\n", used, used_percent);
            printf("Avail %d %d\n", avail, avail_percent);
            printf("Bad %d %d\n", bad, bad_percent);
            printf("Total number of clusters: %d\n", total);
            printf("Total number used: %d\n", used);
            if (avail == 0)
                printf("***Disk full***\n");
            free(cmdLine);
            return 0;
        }
    } else if (!strcmp(args[0], "-print")) {
        print_disk_formatted();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "sc")) {
        read_disk_header();
        if (g_total_clusters >= 65535) {
            printf("Max cluster count reached.\n");
            free(cmdLine);
            return 1;
        }
        if (argc >= 3) {
            int inputIsText = (!strcmp(args[1], "-t") || !strcmp(args[1], "-h")) ? 1 : 0;
            FILE *fp = fopen(current_disk_file, "a");
            if (!fp) {
                perror("sc: open disk file");
                free(cmdLine);
                return 1;
            }
            fprintf(fp, "%02X:", g_total_clusters);
            char *hexData = convert_data_to_hex(args[2], inputIsText, g_cluster_size);
            fprintf(fp, "%s\n", hexData);
            free(hexData);
            fclose(fp);
            g_total_clusters++;
            printf("Created new cluster %d.\n", g_total_clusters - 1);
        } else {
            FILE *fp = fopen(current_disk_file, "a");
            if (!fp) {
                perror("sc: open disk file");
                free(cmdLine);
                return 1;
            }
            fprintf(fp, "%02X:", g_total_clusters);
            for (int i = 0; i < g_cluster_size * 2; i++)
                fputc('0', fp);
            fputc('\n', fp);
            fclose(fp);
            g_total_clusters++;
            printf("Created new cluster %d.\n", g_total_clusters - 1);
        }
        free(cmdLine);
        return 0;
    } else {
        /* Fallback: execute external command */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(cmdLine);
            return 1;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            execvp(args[0], args);
            perror("execvp");
            _exit(127);
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
            free(cmdLine);
            return WEXITSTATUS(status);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Interactive Shell
 *
 * When UNIT_TEST is defined the interactive shell is simulated so that tests do not block.
 * -------------------------------------------------------------------------*/
#ifdef UNIT_TEST
void interactive_shell(void) {
    printf("[UNIT_TEST] Interactive shell skipped.\n");
    return;
}
#else
void interactive_shell(void) {
    printf("[INTERACTIVE MODE] Type 'exit' to leave interactive mode.\n");
    char line[1024] = {0};
    int len = 0;
    g_interactive_history = load_history(&g_interactive_history_count);
    if (!g_interactive_history) {
        g_interactive_history = malloc(sizeof(char*));
        g_interactive_history_count = 0;
    }
    int currHistIndex = g_interactive_history_count;
    enable_raw_mode();
    while (shell_running) {
        if (g_history_cleared) {
            if (g_interactive_history)
                free_history(g_interactive_history, g_interactive_history_count);
            g_interactive_history = malloc(sizeof(char*));
            g_interactive_history_count = 0;
            g_history_cleared = 0;
            currHistIndex = 0;
        }
        printf("shell> ");
        fflush(stdout);
        len = 0;
        memset(line, 0, sizeof(line));
        currHistIndex = g_interactive_history_count;
        while (1) {
            char c;
            ssize_t nread = read(STDIN_FILENO, &c, 1);
            if (nread <= 0)
                break;
            if (c == '\r' || c == '\n') {
                write(STDOUT_FILENO, "\n", 1);
                break;
            } else if (c == 127) {
                if (len > 0) {
                    len--;
                    line[len] = '\0';
                    write(STDOUT_FILENO, "\b \b", 3);
                }
            } else if (c == 27) {
                char seq[2];
                if (read(STDIN_FILENO, seq, 2) == 2) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'A') {
                            if (currHistIndex > 0) {
                                currHistIndex--;
                                printf("\r\33[2Kshell> %s", g_interactive_history[currHistIndex]);
                                fflush(stdout);
                                strcpy(line, g_interactive_history[currHistIndex]);
                                len = strlen(line);
                            }
                        } else if (seq[1] == 'B') {
                            if (currHistIndex < g_interactive_history_count - 1) {
                                currHistIndex++;
                                printf("\r\33[2Kshell> %s", g_interactive_history[currHistIndex]);
                                fflush(stdout);
                                strcpy(line, g_interactive_history[currHistIndex]);
                                len = strlen(line);
                            } else {
                                currHistIndex = g_interactive_history_count;
                                printf("\r\33[2Kshell> ");
                                fflush(stdout);
                                len = 0;
                                line[0] = '\0';
                            }
                        }
                    }
                }
            } else {
                line[len++] = c;
                write(STDOUT_FILENO, &c, 1);
            }
        }
        disable_raw_mode();
        if (len > 0) {
            submit_single_command(line);
            g_interactive_history = realloc(g_interactive_history, sizeof(char*) * (g_interactive_history_count + 2));
            g_interactive_history[g_interactive_history_count] = strdup(line);
            g_interactive_history_count++;
            enable_raw_mode();
        }
    }
    disable_raw_mode();
    free_history(g_interactive_history, g_interactive_history_count);
}
#endif

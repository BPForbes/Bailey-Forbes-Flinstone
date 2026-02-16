#include "interpreter.h"
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "disk.h"
#include "cluster.h"
#include "fs.h"
#include "mem_domain.h"
#include "fs_service_glue.h"
#include "fs_types.h"
#include "path_log.h"
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
    if (!strcmp(trimmed, "help")) {
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
    if (!strncmp(trimmed, "bios", 4) && (trimmed[4] == '\0' || trimmed[4] == ' ')) {
        char *arg = trimmed[4] ? trim_whitespace(trimmed + 4) : "";
        int skip_confirm = 0;
        if (arg[0] == '-' && (arg[1] == 'y' || arg[1] == 'Y') && (arg[2] == '\0' || arg[2] == ' '))
            skip_confirm = 1;
        if (!skip_confirm && isatty(STDIN_FILENO)) {
            printf("Reboot into BIOS/UEFI firmware setup? [y/N]: ");
            fflush(stdout);
            char resp[16];
            if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
                printf("Cancelled.\n");
                free(tokenBuf);
                return 0;
            }
        } else if (!skip_confirm) {
            printf("Use 'bios -y' to reboot in non-interactive mode.\n");
            free(tokenBuf);
            return 0;
        }
        printf("Rebooting to firmware setup (systemctl reboot --firmware-setup)...\n");
        fflush(stdout);
        if (system("systemctl reboot --firmware-setup 2>/dev/null") == 0) {
            /* Will not return on success */
        } else if (system("systemctl reboot --firmware 2>/dev/null") == 0) {
            /* Alternate flag on some systems */
        } else {
            printf("Failed. Try: sudo systemctl reboot --firmware-setup\n");
        }
        free(tokenBuf);
        return 0;
    }
    if (!strncmp(trimmed, "make ", 5)) {
        char *filename = trim_whitespace(trimmed + 5);
        if (*filename) {
            char rpath[CWD_MAX];
            resolve_path(filename, rpath, sizeof(rpath));
            if (g_fm_service) {
                fm_create_file(g_fm_service, rpath);
                path_log_record(PATH_OP_CREATE, rpath);
                printf("Creating file '%s'. Enter lines (end with 'EOF'):\n", rpath);
                char content[4096] = {0};
                size_t off = 0;
                char buf[256];
                while (1) {
                    printf("file> ");
                    fflush(stdout);
                    if (!fgets(buf, sizeof(buf), stdin)) break;
                    buf[strcspn(buf, "\n")] = '\0';
                    if (!strcmp(buf, "EOF")) {
                        printf("Done writing '%s'.\n", rpath);
                        break;
                    }
                    if (off + strlen(buf) + 2 < sizeof(content)) {
                        off += (size_t)snprintf(content + off, sizeof(content) - off, "%s\n", buf);
                    }
                }
                fm_save_text(g_fm_service, rpath, content);
            } else {
                do_make_file(rpath);
            }
        }
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
    if (!strcmp(args[0], "cd")) {
        if (argc < 2) {
            printf("%s\n", g_cwd);
            free(cmdLine);
            return 0;
        }
        char resolved[CWD_MAX];
        resolve_path(args[1], resolved, sizeof(resolved));
        if (chdir(resolved) == 0) {
            if (getcwd(g_cwd, sizeof(g_cwd)) == NULL)
                strncpy(g_cwd, resolved, sizeof(g_cwd) - 1);
            path_log_record(PATH_OP_CD, g_cwd);
            printf("%s\n", g_cwd);
        } else {
            perror("cd");
            free(cmdLine);
            return 1;
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "createdisk")) {
        if (argc < 4) {
            printf("Usage: createdisk <volume> <rowCount> <nibbleCount> [ -y | -n ]\n");
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
        if (argc >= 5 && (!strcmp(args[4], "-y") || !strcmp(args[4], "-Y")))
            interactive_shell();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "format")) {
        if (argc < 5) {
            printf("Usage: format <disk_file> <volume> <rowCount> <nibbleCount>\n");
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
    } else if (!strcmp(args[0], "setdisk")) {
        if (argc < 2) {
            printf("Usage: setdisk <disk_file>\n");
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
    } else if (!strcmp(args[0], "version")) {
        if (argc >= 2) {
            if (!strcmp(args[1], "-y") || !strcmp(args[1], "-Y")) {
                if (unlink(HISTORY_FILE) == 0)
                    printf("History file deleted.\n");
                else
                    perror("Failed to delete history file");
            } else if (!strcmp(args[1], "-n") || !strcmp(args[1], "-N")) {
                printf("History file retained.\n");
            } else {
                printf("Usage: version [ -y | -n ]\n");
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
    } else if (!strcmp(args[0], "listclusters")) {
        list_clusters_contents();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "listdirs")) {
        char rpath[CWD_MAX];
        resolve_path(".", rpath, sizeof(rpath));
        if (g_fm_service) {
            fs_node_t *nodes;
            int count;
            if (fm_list(g_fm_service, rpath, &nodes, &count) == 0) {
                path_log_record(PATH_OP_DIR, rpath);
                printf("Directories in current path:\n");
                for (int i = 0; i < count; i++)
                    if (nodes[i].type == NODE_DIR)
                        printf("  %s\n", nodes[i].name);
                fs_nodes_free(nodes, count);
            }
        } else {
            path_log_record(PATH_OP_DIR, rpath);
            list_directories();
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "search")) {
        int searchMode = 0;
        char *searchStr = NULL;
        if (argc >= 3 && (!strcmp(args[2], "-h") || !strcmp(args[2], "-t"))) {
            searchStr = args[1];
            searchMode = (!strcmp(args[2], "-h")) ? 1 : 0;
        } else if (argc >= 2) {
            searchStr = args[1];
        } else {
            printf("Usage: search <searchtext> [ -t |-h ]\n");
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
                mem_domain_free(MEM_DOMAIN_FS, ascii);
            }
        }
        fclose(fp);
        if (!found)
            printf("'%s' not found.\n", searchStr);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "writecluster")) {
        if (argc < 4) {
            printf("Usage: writecluster <cluster_index> -t|-h <data>\n");
            free(cmdLine);
            return 1;
        }
        int clu = atoi(args[1]);
        int inputIsText = (!strcmp(args[2], "-t")) ? 1 : 0;
        process_write_cluster(clu, args[3], inputIsText);
        calculate_storage_breakdown_for_cluster(clu);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "delcluster")) {
        if (argc < 2) {
            printf("Usage: delcluster <cluster_index>\n");
            free(cmdLine);
            return 1;
        }
        int clu = atoi(args[1]);
        delete_cluster(clu);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "update")) {
        if (argc < 4) {
            printf("Usage: update <cluster_index> -t|-h <data>\n");
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
    } else if (!strcmp(args[0], "type")) {
        if (argc < 2) {
            printf("Usage: type <filename>\n");
            free(cmdLine);
            return 1;
        }
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        if (g_fm_service) {
            char buf[4096];
            if (fm_read_text(g_fm_service, rpath, buf, sizeof(buf)) >= 0) {
                path_log_record(PATH_OP_READ, rpath);
                printf("%s", buf);
            } else
                perror("type");
        } else {
            path_log_record(PATH_OP_READ, rpath);
            cat_file(rpath);
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "redirect")) {
        if (argc < 2) {
            printf("Usage: redirect <filename> or \"redirect off\"\n");
            free(cmdLine);
            return 1;
        }
        do_redirect_output(args[1]);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "initdisk")) {
        if (argc < 3) {
            printf("Usage: initdisk <cluster_count> <cluster_size>\n");
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
    } else if (!strcmp(args[0], "rerun")) {
        if (argc < 2) {
            printf("Usage: rerun <N>\n");
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
    } else if (!strcmp(args[0], "import")) {
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
            printf("Usage: import <textfile> <txtfile> [clusters clusterSize]\n");
            free(cmdLine);
            return 1;
        }
    } else if (!strcmp(args[0], "du")) {
        /* Modified disk usage command.
           Without additional parameter, it prints a short disk usage report.
           With the -dtl parameter it prints detailed information for specified clusters.
        */
        if (argc >= 2 && !strcmp(args[1], "dtl")) {
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
    } else if (!strcmp(args[0], "printdisk")) {
        print_disk_formatted();
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "dir")) {
        char rpath[CWD_MAX];
        resolve_path((argc >= 2 && args[1][0] != '-') ? args[1] : ".", rpath, sizeof(rpath));
        if (g_fm_service) {
            fs_node_t *nodes;
            int count;
            if (fm_list(g_fm_service, rpath, &nodes, &count) == 0) {
                path_log_record(PATH_OP_DIR, rpath);
                printf("Files in '%s':\n", rpath);
                for (int i = 0; i < count; i++)
                    printf("  %s\n", nodes[i].name);
                fs_nodes_free(nodes, count);
            } else {
                printf("Cannot open '%s'\n", rpath);
            }
        } else {
            list_files(rpath);
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "mkdir")) {
        if (argc < 2) {
            printf("Usage: mkdir <directory>\n");
            free(cmdLine);
            return 1;
        }
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        if (g_fm_service) {
            if (fm_create_dir(g_fm_service, rpath) == 0) {
                path_log_record(PATH_OP_CREATE, rpath);
                printf("Directory '%s' created.\n", rpath);
            } else
                perror("mkdir");
        } else {
            create_directory(rpath);
            path_log_record(PATH_OP_CREATE, rpath);
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "rmdir")) {
        if (argc < 2) {
            printf("Usage: rmdir <directory>\n");
            free(cmdLine);
            return 1;
        }
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        if (g_fm_service) {
            if (fm_delete(g_fm_service, rpath) == 0) {
                path_log_record(PATH_OP_DELETE, rpath);
                printf("Directory '%s' removed.\n", rpath);
            } else
                perror("rmdir");
        } else if (rmdir(rpath) == 0) {
            path_log_record(PATH_OP_DELETE, rpath);
            printf("Directory '%s' removed.\n", rpath);
        } else {
            perror("rmdir");
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "rmtree")) {
        if (argc < 2) {
            printf("Usage: rmtree <directory>\n");
            free(cmdLine);
            return 1;
        }
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        path_log_record(PATH_OP_DELETE, rpath);
        remove_directory_recursive(rpath);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "cat")) {
        if (argc < 2) {
            printf("Usage: cat <filename>\n");
            free(cmdLine);
            return 1;
        }
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        if (g_fm_service) {
            char buf[4096];
            if (fm_read_text(g_fm_service, rpath, buf, sizeof(buf)) >= 0) {
                path_log_record(PATH_OP_READ, rpath);
                printf("%s", buf);
            } else
                perror("cat");
        } else {
            path_log_record(PATH_OP_READ, rpath);
            cat_file(rpath);
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "write")) {
        /* write <file> <content> - write content to file */
        if (argc < 3) {
            printf("Usage: write <filename> <content>\n");
            free(cmdLine);
            return 1;
        }
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        char content[4096] = {0};
        for (int i = 2; i < argc && (size_t)(strlen(content) + strlen(args[i]) + 2) < sizeof(content); i++) {
            if (i > 2) strcat(content, " ");
            strcat(content, args[i]);
        }
        if (g_fm_service) {
            if (fm_save_text(g_fm_service, rpath, content) == 0) {
                path_log_record(PATH_OP_WRITE, rpath);
                printf("Wrote to '%s'\n", rpath);
            } else
                perror("write");
        } else {
            FILE *f = fopen(rpath, "w");
            if (f) {
                fputs(content, f);
                fclose(f);
                path_log_record(PATH_OP_WRITE, rpath);
                printf("Wrote to '%s'\n", rpath);
            } else perror("write");
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "mv")) {
        if (argc < 3) {
            printf("Usage: mv <src> <dst>\n");
            free(cmdLine);
            return 1;
        }
        char srcpath[CWD_MAX], dstpath[CWD_MAX];
        resolve_path(args[1], srcpath, sizeof(srcpath));
        resolve_path(args[2], dstpath, sizeof(dstpath));
        if (g_fm_service) {
            if (fm_move(g_fm_service, srcpath, dstpath) == 0) {
                path_log_record(PATH_OP_MOVE, srcpath);
                path_log_record(PATH_OP_MOVE, dstpath);
                printf("Moved '%s' to '%s'\n", srcpath, dstpath);
            } else
                perror("mv");
        } else {
            if (rename(srcpath, dstpath) == 0) {
                path_log_record(PATH_OP_MOVE, srcpath);
                path_log_record(PATH_OP_MOVE, dstpath);
                printf("Moved '%s' to '%s'\n", srcpath, dstpath);
            } else
                perror("mv");
        }
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "where") || !strcmp(args[0], "loc")) {
        int n = (argc >= 2) ? atoi(args[1]) : 16;
        path_log_print(n);
        free(cmdLine);
        return 0;
    } else if (!strcmp(args[0], "addcluster")) {
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

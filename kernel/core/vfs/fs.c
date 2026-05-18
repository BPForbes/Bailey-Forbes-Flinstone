#include "fs.h"
#include "common.h"
#include "util.h"
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

struct fs_rmtree_entry {
    char path[1024];
    int depth;
    int isDir;
};

static void fs_rmtree_scan_dir(const char *dir, int depth, struct fs_rmtree_entry **p_entries,
                               int *p_entryCount, int *p_entryCapacity) {
    DIR *dp = opendir(dir);
    if (!dp)
        return;
    struct dirent *entry;
    while ((entry = readdir(dp))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char fullPath[1024];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, entry->d_name);
        if (*p_entryCount >= *p_entryCapacity) {
            *p_entryCapacity *= 2;
            struct fs_rmtree_entry *ne =
                realloc(*p_entries, sizeof(struct fs_rmtree_entry) * (size_t)*p_entryCapacity);
            if (!ne) {
                perror("realloc");
                closedir(dp);
                return;
            }
            *p_entries = ne;
        }
        strncpy((*p_entries)[*p_entryCount].path, fullPath, sizeof((*p_entries)[*p_entryCount].path) - 1);
        (*p_entries)[*p_entryCount].path[sizeof((*p_entries)[*p_entryCount].path) - 1] = '\0';
        (*p_entries)[*p_entryCount].depth = depth;
        struct stat st;
        if (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode)) {
            (*p_entries)[*p_entryCount].isDir = 1;
            (*p_entryCount)++;
            fs_rmtree_scan_dir(fullPath, depth + 1, p_entries, p_entryCount, p_entryCapacity);
        } else {
            (*p_entries)[*p_entryCount].isDir = 0;
            (*p_entryCount)++;
        }
    }
    closedir(dp);
}

static int fs_rmtree_compare_entries(const void *a, const void *b) {
    const struct fs_rmtree_entry *ea = (const struct fs_rmtree_entry *)a;
    const struct fs_rmtree_entry *eb = (const struct fs_rmtree_entry *)b;
    return eb->depth - ea->depth;
}

void list_files(const char *dir) {
    DIR *dp = opendir(dir);
    if (!dp) {
        printf("Cannot open '%s': %s\n", dir, strerror(errno));
        return;
    }
    struct dirent *entry;
    printf("Files in '%s':\n", dir);
    while ((entry = readdir(dp))) {
        if (entry->d_type == DT_REG)
            printf("  %s\n", entry->d_name);
    }
    closedir(dp);
}

int create_directory(const char *d) {
    if (mkdir(d, 0755) == 0) {
        printf("Directory '%s' created.\n", d);
        return 0;
    }
    perror("mkdir");
    return -1;
}

void list_directories(void) {
    DIR *dp = opendir(".");
    if (!dp) {
        perror("opendir");
        return;
    }
    struct dirent *entry;
    printf("Directories in current path:\n");
    while ((entry = readdir(dp))) {
        if (entry->d_type == DT_DIR &&
            strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            printf("  %s\n", entry->d_name);
    }
    closedir(dp);
}

int remove_directory_recursive(const char *d) {
    struct fs_rmtree_entry *entries = NULL;
    int entryCount = 0, entryCapacity = 100;
    entries = malloc(sizeof(struct fs_rmtree_entry) * (size_t)entryCapacity);
    if (!entries) {
        perror("malloc");
        return -1;
    }
    fs_rmtree_scan_dir(d, 0, &entries, &entryCount, &entryCapacity);
    qsort(entries, (size_t)entryCount, sizeof(struct fs_rmtree_entry), fs_rmtree_compare_entries);
    for (int i = 0; i < entryCount; i++) {
        if (!entries[i].isDir) {
            if (remove(entries[i].path) != 0)
                perror("remove file");
        }
    }
    for (int i = 0; i < entryCount; i++) {
        if (entries[i].isDir) {
            if (rmdir(entries[i].path) != 0)
                perror("rmdir");
        }
    }
    free(entries);
    if (rmdir(d) != 0) {
        perror("rmdir");
        return -1;
    }
    printf("Directory '%s' removed.\n", d);
    return 0;
}

void cat_file(const char *f) {
    FILE *fp = fopen(f, "r");
    if (!fp) {
        perror("cat");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), fp))
        printf("%s", line);
    fclose(fp);
}

void do_make_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("make file");
        return;
    }
    printf("Creating file '%s'. Enter lines (end with 'EOF'):\n", filename);
    while (1) {
        char buf[256];
        printf("file> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) {
            printf("\nStopped.\n");
            break;
        }
        buf[strcspn(buf, "\n")] = '\0';
        if (!strcmp(buf, "EOF")) {
            printf("Done writing '%s'.\n", filename);
            break;
        }
        fprintf(fp, "%s\n", buf);
    }
    fclose(fp);
}

void do_redirect_output(const char *filename) {
    if (!strcmp(filename, "off")) {
        fflush(stdout);
        dup2(original_stdout_fd, fileno(stdout));
        printf("Output redirection off.\n");
        return;
    }
    FILE *fp = freopen(filename, "w", stdout);
    if (!fp)
        perror("do_redirect_output");
    else
        printf("Redirecting output to '%s'.\n", filename);
}

void import_text_drive(const char *textFile, const char *dest_disk, int overrideClusters, int overrideSize) {
    FILE *fin = fopen(textFile, "r");
    if (!fin) {
        fprintf(stderr, "Cannot open text drive listing: %s\n", textFile);
        return;
    }
    static char *linesStorage[65536];
    for (int i = 0; i < 65536; i++)
        linesStorage[i] = NULL;
    char line[256];
    while (fgets(line, sizeof(line), fin)) {
        char *trim = trim_whitespace(line);
        if (!trim || !*trim)
            continue;
        if (!strncmp(trim, "XX:", 3))
            continue;
        char *colon = strchr(trim, ':');
        if (!colon)
            continue;
        *colon = '\0';
        char *idxStr = trim_whitespace(trim);
        char *hexData = trim_whitespace(colon + 1);
        int clusterIndex = (int)strtol(idxStr, NULL, 16);
        if (clusterIndex < 0 || clusterIndex > 65535)
            continue;
        int lenHex = (int)strlen(hexData);
        if (lenHex < 2)
            continue;
        char *buf = malloc(lenHex + 1);
        if (!buf)
            continue;
        asm_mem_copy(buf, hexData, (size_t)lenHex);
        buf[lenHex] = '\0';
        if (linesStorage[clusterIndex])
            free(linesStorage[clusterIndex]);
        linesStorage[clusterIndex] = buf;
    }
    fclose(fin);
    int maxClusters = 32, clusterSz = 32;
    if (overrideClusters > 0 && overrideSize > 0) {
        maxClusters = overrideClusters;
        clusterSz = overrideSize;
    } else if (linesStorage[0]) {
        int len = (int)strlen(linesStorage[0]);
        clusterSz = len / 2;
        int count = 0;
        for (int i = 0; i < 65536; i++) {
            if (linesStorage[i])
                count++;
        }
        if (count > 0)
            maxClusters = count;
    }
    FILE *out = fopen(dest_disk, "w");
    if (!out) {
        fprintf(stderr, "Cannot open output disk file: %s\n", dest_disk);
        for (int i = 0; i < 65536; i++)
            if (linesStorage[i])
                free(linesStorage[i]);
        return;
    }
    char *ruler = NULL;
    size_t cs = (size_t)clusterSz;
    if (cs <= (SIZE_MAX - 1u) / 2u)
        ruler = malloc(cs * 2u + 1u);
    if (ruler) {
        const char *digits = "0123456789ABCDEF";
        for (size_t j = 0; j < cs * 2u; j++)
            ruler[j] = digits[(int)(j % 16u)];
        ruler[cs * 2u] = '\0';
        fprintf(out, "XX:%s\n", ruler);
        free(ruler);
    }
    for (int c = 0; c < maxClusters; c++) {
        if (linesStorage[c])
            fprintf(out, "%02X:%s\n", c, linesStorage[c]);
        else {
            if (cs > (SIZE_MAX - 1u) / 2u) {
                fprintf(stderr, "import_text_drive: cluster size overflow for zero-fill buffer\n");
                fclose(out);
                for (int i = 0; i < 65536; i++)
                    if (linesStorage[i])
                        free(linesStorage[i]);
                return;
            }
            char *zeros = malloc(cs * 2u + 1u);
            if (!zeros) {
                fprintf(stderr, "import_text_drive: out of memory for zero-fill buffer\n");
                fclose(out);
                for (int i = 0; i < 65536; i++)
                    if (linesStorage[i])
                        free(linesStorage[i]);
                return;
            }
            for (size_t i = 0; i < cs * 2u; i++)
                zeros[i] = '0';
            zeros[cs * 2u] = '\0';
            fprintf(out, "%02X:%s\n", c, zeros);
            free(zeros);
        }
    }
    fclose(out);
    printf("Imported text drive listing => %s\n", dest_disk);
    for (int i = 0; i < 65536; i++)
        if (linesStorage[i])
            free(linesStorage[i]);
}

#include "fs.h"
#include "common.h"
#include "util.h"
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

struct fs_rmtree_entry {
    char path[PATH_MAX];
    int depth;
    int isDir;
};

/**
 * Compare two fs_rmtree_entry records by recursion depth for descending order.
 *
 * @param a Pointer to the first `struct fs_rmtree_entry`.
 * @param b Pointer to the second `struct fs_rmtree_entry`.
 * @returns A value greater than `0` if `b` has a greater depth than `a` (so `b` should sort before `a`),
 *          `0` if both depths are equal,
 *          a value less than `0` if `a` has a greater depth than `b`.
 */
static int fs_rmtree_entry_cmp(const void *a, const void *b) {
    const struct fs_rmtree_entry *ea = (const struct fs_rmtree_entry *)a;
    const struct fs_rmtree_entry *eb = (const struct fs_rmtree_entry *)b;
    return eb->depth - ea->depth;
}

/**
 * Recursively scans a directory tree and appends discovered entries to a dynamic array.
 *
 * This function walks `dir`, records each entry's full path, recursion depth, and whether it
 * is a directory into the provided `entries` array. The array may be grown via `realloc`
 * when capacity is reached. If `opendir` fails the scan for `dir` is skipped; on `realloc`
 * failure the scan is aborted and an error is reported.
 *
 * @param dir Path of the directory to scan.
 * @param depth Current recursion depth for entries added from this invocation (use 0 for root).
 * @param entries Pointer to the array of `fs_rmtree_entry` records; may be reallocated and updated.
 * @param entryCount Pointer to the number of entries currently stored; incremented as entries are appended.
 * @param entryCapacity Pointer to the current capacity of the `entries` array; updated if the array grows.
 */
static void fs_rmtree_scan(const char *dir, int depth,
                           struct fs_rmtree_entry **entries,
                           int *entryCount, int *entryCapacity) {
    DIR *dp = opendir(dir);
    if (!dp)
        return;
    struct dirent *entry;
    while ((entry = readdir(dp))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char fullPath[PATH_MAX];
        int plen = snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, entry->d_name);
        if (plen < 0 || (size_t)plen >= sizeof(fullPath)) {
            fprintf(stderr, "remove_directory_recursive: path too long, skipping entry\n");
            continue;
        }
        if (*entryCount >= *entryCapacity) {
            int newCap = *entryCapacity * 2;
            struct fs_rmtree_entry *new_entries =
                realloc(*entries, sizeof(struct fs_rmtree_entry) * (size_t)newCap);
            if (!new_entries) {
                perror("realloc");
                closedir(dp);
                return;
            }
            *entries = new_entries;
            *entryCapacity = newCap;
        }
        strncpy((*entries)[*entryCount].path, fullPath, sizeof((*entries)[*entryCount].path) - 1);
        (*entries)[*entryCount].path[sizeof((*entries)[*entryCount].path) - 1] = '\0';
        (*entries)[*entryCount].depth = depth;
        struct stat st;
        if (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode)) {
            (*entries)[*entryCount].isDir = 1;
            (*entryCount)++;
            fs_rmtree_scan(fullPath, depth + 1, entries, entryCount, entryCapacity);
        } else {
            (*entries)[*entryCount].isDir = 0;
            (*entryCount)++;
        }
    }
    closedir(dp);
}

/**
 * Prints the names of regular files contained in the specified directory to stdout.
 *
 * On failure to open the directory, prints an error message describing the failure
 * (using strerror(errno)) and returns without printing entries.
 *
 * @param dir Path to the directory to list (null-terminated string).
 */
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

void create_directory(const char *d) {
    if (mkdir(d, 0755) == 0)
        printf("Directory '%s' created.\n", d);
    else
        perror("mkdir");
}

/**
 * Print the names of subdirectories in the current working directory to stdout.
 *
 * The listing excludes the entries "." and "..". If the current directory cannot
 * be opened the function prints an error message to stderr via perror and
 * returns without producing a listing.
 */
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

/**
 * Remove a directory tree at the given path, deleting files first and directories afterwards.
 *
 * Scans the directory tree, sorts entries by depth (deepest first), removes regular files, then
 * removes directories, and finally removes the top-level directory. Partial deletion may occur
 * if memory allocation or scanning fails; individual remove/rmdir failures are reported via
 * perror but do not halt the cleanup loop.
 *
 * @param d Path to the directory to remove.
 * @returns 0 on successful removal of the directory and its contents, -1 on error.
 */
int remove_directory_recursive(const char *d) {
    struct fs_rmtree_entry *entries = NULL;
    int entryCount = 0, entryCapacity = 100;
    entries = malloc(sizeof(struct fs_rmtree_entry) * (size_t)entryCapacity);
    if (!entries) {
        perror("malloc");
        return -1;
    }
    fs_rmtree_scan(d, 0, &entries, &entryCount, &entryCapacity);
    qsort(entries, (size_t)entryCount, sizeof(struct fs_rmtree_entry), fs_rmtree_entry_cmp);
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

void import_text_drive(const char *textFile, const char *destTxt, int overrideClusters, int overrideSize) {
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
    FILE *out = fopen(destTxt, "w");
    if (!out) {
        fprintf(stderr, "Cannot open output text file: %s\n", destTxt);
        for (int i = 0; i < 65536; i++)
            if (linesStorage[i])
                free(linesStorage[i]);
        return;
    }
    char *ruler = malloc(clusterSz * 2 + 1);
    if (ruler) {
        const char *digits = "0123456789ABCDEF";
        for (int j = 0; j < clusterSz * 2; j++)
            ruler[j] = digits[j % 16];
        ruler[clusterSz * 2] = '\0';
        fprintf(out, "XX:%s\n", ruler);
        free(ruler);
    }
    for (int c = 0; c < maxClusters; c++) {
        if (linesStorage[c])
            fprintf(out, "%02X:%s\n", c, linesStorage[c]);
        else {
            char *zeros = malloc(clusterSz * 2 + 1);
            if (zeros) {
                for (int i = 0; i < clusterSz * 2; i++)
                    zeros[i] = '0';
                zeros[clusterSz * 2] = '\0';
                fprintf(out, "%02X:%s\n", c, zeros);
                free(zeros);
            }
        }
    }
    fclose(out);
    printf("Imported text drive listing => %s\n", destTxt);
    for (int i = 0; i < 65536; i++)
        if (linesStorage[i])
            free(linesStorage[i]);
}

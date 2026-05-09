#include "common.h"
#include "cmd_decl.h"
#include "cluster.h"
#include "mem_domain.h"
#include "util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Searches the configured disk file for a given text and prints matching sectors.
 *
 * Performs either a raw-hex substring search or an ASCII search after converting
 * hex data to ASCII. Mode is selected with the second argument flag: "-h" to
 * search the raw hex data, "-t" (or omitted) to convert hex to ASCII and search
 * that text. Lines beginning with "XX:" or without a ':' separator are ignored.
 *
 * @returns 1 if the command usage was invalid (usage message printed); 0 otherwise.
 */
int cmd_search_run(int argc, char **argv) {
    char **args = argv;
    int searchMode = 0;
    char *searchStr = NULL;
    if (argc >= 3 && (!strcmp(args[2], "-h") || !strcmp(args[2], "-t"))) {
        searchStr = args[1];
        searchMode = (!strcmp(args[2], "-h")) ? 1 : 0;
    } else if (argc >= 2) {
        searchStr = args[1];
    } else {
        printf("Usage: search <searchtext> [ -t |-h ]\n");
        return 1;
    }
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file.\n");
        return 0;
    }
    char linebuf[512];
    int found = 0;
    if (fgets(linebuf, sizeof(linebuf), fp) == NULL) {
        fclose(fp);
        printf("Disk file is empty.\n");
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
    return 0;
}

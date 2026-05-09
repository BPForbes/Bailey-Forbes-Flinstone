#include "cmd_decl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_bios_maybe(char *trimmed) {
    if (strncmp(trimmed, "bios", 4) != 0 || (trimmed[4] != '\0' && trimmed[4] != ' '))
        return 0;
    char *arg = trimmed[4] ? trimmed + 4 : "";
    while (*arg == ' ' || *arg == '\t')
        arg++;
    int skip_confirm = 0;
    if (arg[0] == '-' && (arg[1] == 'y' || arg[1] == 'Y') && (arg[2] == '\0' || arg[2] == ' '))
        skip_confirm = 1;
    if (!skip_confirm && isatty(STDIN_FILENO)) {
        printf("Reboot into BIOS/UEFI firmware setup? [y/N]: ");
        fflush(stdout);
        char resp[16];
        if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
            printf("Cancelled.\n");
            return 1;
        }
    } else if (!skip_confirm) {
        printf("Use 'bios -y' to reboot in non-interactive mode.\n");
        return 1;
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
    return 1;
}

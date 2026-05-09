#include "cmd_decl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Handle a `bios` command: optionally confirm and attempt to reboot to firmware setup.
 *
 * Parses `trimmed` for the `bios` command (must be exactly "bios" or "bios " followed by args),
 * accepts an optional `-y`/`-Y` flag to skip confirmation, prompts the user when appropriate,
 * and invokes systemctl to request a reboot into firmware/UEFI setup.
 *
 * @param trimmed Command string starting with "bios" (may include trailing arguments).
 * @returns `0` if `trimmed` does not start with "bios" followed by end-of-string or a space;
 *          `1` if the command was handled (including when cancelled, when non-interactive without `-y`,
 *          or after attempting the reboot). On successful reboot the function may not return.
 */
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

/*
 * WASM / host-lab entry: identity + ramfs + labdisk + labnet + interactive shell.
 * Do not compile kernel.c here (IDT / hlt). Prompt is shell> with -DFL_WASM_SHELL_PROMPT.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "identity.h"
#include "labdisk.h"
#include "labnet.h"
#include "ramfs.h"
#include "keyboard.h"
#include "serial.h"
#include "shell.h"
#include "vga.h"

EMSCRIPTEN_KEEPALIVE void wasm_shell_init(void)
{
    fl_fs_serial_init();
    fl_fs_vga_init();
    fl_fs_kbd_init();
    fl_fs_serial_puts("FLINTSTONE_KERNEL_BOOT_OK\n");
    fl_fs_identity_init();
    fl_fs_ramfs_init();
    fl_fs_labdisk_init();
    fl_fs_labnet_init();
    fl_fs_shell_init();
}

EMSCRIPTEN_KEEPALIVE void wasm_shell_type(int c)
{
    fl_fs_shell_input((char)c);
}

EMSCRIPTEN_KEEPALIVE void wasm_shell_line(const char *line)
{
    if (!line)
        return;
    while (*line)
        fl_fs_shell_input(*line++);
    fl_fs_shell_input('\n');
}

#ifndef __EMSCRIPTEN__
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    wasm_shell_init();
    if (argc > 1) {
        int i;
        for (i = 1; i < argc; i++)
            wasm_shell_line(argv[i]);
        return 0;
    }
    {
        char buf[256];
        while (fgets(buf, sizeof(buf), stdin)) {
            size_t n = strlen(buf);
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
                buf[--n] = '\0';
            wasm_shell_line(buf);
        }
    }
    return 0;
}
#endif

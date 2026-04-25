#include "common.h"
#include "fs_jail.h"
#include <stdio.h>

int main(void) {
    g_vm_mode = 1;
    g_vm_root[0] = '\0';
    fs_jail_init();
    return 0;
}

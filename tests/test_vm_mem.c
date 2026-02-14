#include "vm/vm_mem.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    vm_mem_t mem;
    if (vm_mem_init(&mem) != 0) {
        fprintf(stderr, "vm_mem_init failed\n");
        return 1;
    }

    /* Test write/read */
    const char hello[] = "hello";
    if (vm_mem_load(&mem, 0x1000, hello, sizeof(hello)) != 0) {
        fprintf(stderr, "vm_mem_load failed\n");
        vm_mem_destroy(&mem);
        return 1;
    }
    char buf[16];
    if (vm_mem_read(&mem, 0x1000, buf, sizeof(hello)) != 0) {
        fprintf(stderr, "vm_mem_read failed\n");
        vm_mem_destroy(&mem);
        return 1;
    }
    if (memcmp(buf, hello, sizeof(hello)) != 0) {
        fprintf(stderr, "data mismatch\n");
        vm_mem_destroy(&mem);
        return 1;
    }

    /* Test read8/write8 */
    vm_mem_write8(&mem, 0x2000, 0xAB);
    if (vm_mem_read8(&mem, 0x2000) != 0xAB) {
        fprintf(stderr, "read8/write8 mismatch\n");
        vm_mem_destroy(&mem);
        return 1;
    }

    vm_mem_destroy(&mem);
    printf("vm_mem tests OK\n");
    return 0;
}

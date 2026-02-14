/**
 * Property-style tests for mem_asm vs libc (memcpy/memset).
 * Build: make test_mem_asm
 * Run with -fsanitize=address,undefined when not using ASM allocator.
 */
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_mem_copy_vs_memcpy(void) {
    char src[256], dst1[256], dst2[256];
    for (int i = 0; i < 256; i++) src[i] = (char)(i ^ 0x5A);
    for (int n = 0; n <= 128; n++) {
        memset(dst1, 0, sizeof(dst1));
        memset(dst2, 0, sizeof(dst2));
        memcpy(dst1, src, n);
        asm_mem_copy(dst2, src, n);
        ASSERT(memcmp(dst1, dst2, n) == 0);
    }
    return 0;
}

static int test_mem_zero_vs_memset(void) {
    char buf1[256], buf2[256];
    for (int n = 0; n <= 128; n++) {
        memset(buf1, 0x42, sizeof(buf1));
        memset(buf2, 0x42, sizeof(buf2));
        memset(buf1, 0, n);
        asm_mem_zero(buf2, n);
        ASSERT(memcmp(buf1, buf2, 256) == 0);
    }
    return 0;
}

static int test_block_fill_vs_memset(void) {
    char buf1[256], buf2[256];
    for (int bi = 0; bi < 256; bi++) {
        unsigned char b = (unsigned char)bi;
        for (int n = 0; n <= 64; n++) {
            memset(buf1, 0, sizeof(buf1));
            memset(buf2, 0, sizeof(buf2));
            memset(buf1, b, n);
            asm_block_fill(buf2, b, n);
            ASSERT(memcmp(buf1, buf2, 256) == 0);
        }
    }
    return 0;
}

int main(void) {
    printf("test_mem_copy_vs_memcpy... ");
    if (test_mem_copy_vs_memcpy() != 0) return 1;
    printf("OK\n");
    printf("test_mem_zero_vs_memset... ");
    if (test_mem_zero_vs_memset() != 0) return 1;
    printf("OK\n");
    printf("test_block_fill_vs_memset... ");
    if (test_block_fill_vs_memset() != 0) return 1;
    printf("OK\n");
    printf("All mem_asm tests passed.\n");
    return 0;
}

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

static int test_mem_secure_zero_vs_memset(void) {
    char buf1[256], buf2[256], buf3[256];
    /* Every start alignment: the child runs its parent's qword and byte paths. */
    for (int off = 0; off < 8; off++) {
        for (int n = 0; n <= 128; n++) {
            memset(buf1, 0x5C, sizeof(buf1));
            memset(buf2, 0x5C, sizeof(buf2));
            memset(buf3, 0x5C, sizeof(buf3));
            memset(buf1 + off, 0, n);
            asm_mem_secure_zero(buf2 + off, n);
            asm_mem_zero(buf3 + off, n);
            /* Only those n bytes change, exactly as the parent asm_mem_zero changes them. */
            ASSERT(memcmp(buf1, buf2, 256) == 0);
            ASSERT(memcmp(buf2, buf3, 256) == 0);
        }
    }
    return 0;
}

static int test_mem_ct_equal_vs_memcmp(void) {
    unsigned char a[128], b[128];
    for (int i = 0; i < 128; i++) a[i] = b[i] = (unsigned char)(i * 7 + 3);
    ASSERT(asm_mem_ct_equal(a, b, 0) == 1);
    for (int n = 1; n <= 128; n++) {
        ASSERT(asm_mem_ct_equal(a, b, (size_t)n) == 1);
        /* Every position, every single-bit difference, is caught. */
        for (int pos = 0; pos < n; pos++) {
            for (int bit = 0; bit < 8; bit++) {
                b[pos] ^= (unsigned char)(1u << bit);
                ASSERT(asm_mem_ct_equal(a, b, (size_t)n) == 0);
                b[pos] ^= (unsigned char)(1u << bit);
            }
        }
        /* A difference past n is outside the compare. */
        if (n < 128) {
            b[n] ^= 0xFF;
            ASSERT(asm_mem_ct_equal(a, b, (size_t)n) == 1);
            b[n] ^= 0xFF;
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
    printf("test_mem_secure_zero_vs_memset... ");
    if (test_mem_secure_zero_vs_memset() != 0) return 1;
    printf("OK\n");
    printf("test_mem_ct_equal_vs_memcmp... ");
    if (test_mem_ct_equal_vs_memcmp() != 0) return 1;
    printf("OK\n");
    printf("All mem_asm tests passed.\n");
    return 0;
}

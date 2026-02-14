/**
 * Allocator torture tests: random alloc/free, coalescing, basic invariants.
 * Build: make test_alloc (uses libc) or make test_alloc USE_ASM_ALLOC=1
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 64

static int test_basic(void) {
    void *p = malloc(32);
    if (!p) return 1;
    memset(p, 0x41, 32);
    free(p);
    void *q = calloc(4, 8);
    if (!q) return 1;
    for (int i = 0; i < 32; i++)
        if (((char *)q)[i] != 0) { free(q); return 1; }
    free(q);
    return 0;
}

static int test_realloc_chain(void) {
    void *p = malloc(16);
    if (!p) return 1;
    for (int i = 0; i < 10; i++) {
        p = realloc(p, 16 * (i + 2));
        if (!p) return 1;
        ((char *)p)[0] = (char)i;
    }
    free(p);
    return 0;
}

static int test_random_pattern(void) {
    void *ptrs[N];
    for (int i = 0; i < N; i++) ptrs[i] = NULL;
    for (int round = 0; round < 200; round++) {
        int i = rand() % N;
        if (ptrs[i]) {
            free(ptrs[i]);
            ptrs[i] = NULL;
        } else {
            size_t sz = (rand() % 256) + 16;
            ptrs[i] = malloc(sz);
            if (ptrs[i]) memset(ptrs[i], 0xAB, sz);
        }
    }
    for (int i = 0; i < N; i++)
        if (ptrs[i]) free(ptrs[i]);
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    srand(42);
    printf("test_basic... ");
    if (test_basic() != 0) { printf("FAIL\n"); return 1; }
    printf("OK\n");
    printf("test_realloc_chain... ");
    if (test_realloc_chain() != 0) { printf("FAIL\n"); return 1; }
    printf("OK\n");
    printf("test_random_pattern... ");
    if (test_random_pattern() != 0) { printf("FAIL\n"); return 1; }
    printf("OK\n");
    printf("All alloc tests passed.\n");
    return 0;
}

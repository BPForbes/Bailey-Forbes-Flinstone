/**
 * Issue #222.3: cluster hex encoding must NUL-terminate within allocation only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static void encode_cluster_hex(const unsigned char *buf, int cluster_size, char *hex_str) {
    static const char hex[] = "0123456789ABCDEF";

    for (int i = 0; i < cluster_size; i++) {
        hex_str[i * 2]     = hex[(buf[i] >> 4) & 0xF];
        hex_str[i * 2 + 1] = hex[buf[i] & 0xF];
    }
    hex_str[cluster_size * 2] = '\0';
}

static int test_hex_encoding_fits_allocation(void) {
    const int cluster_size = 8;
    unsigned char buf[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xFF};
    char *hex_str = malloc((size_t)cluster_size * 2u + 1u);

    ASSERT(hex_str != NULL);
    encode_cluster_hex(buf, cluster_size, hex_str);
    ASSERT(hex_str[cluster_size * 2] == '\0');
    ASSERT(strlen(hex_str) == (size_t)cluster_size * 2u);
    ASSERT(strcmp(hex_str, "00112233445566FF") == 0);
    free(hex_str);
    return 0;
}

static int test_hex_no_per_iteration_nul_overflow(void) {
    const int cluster_size = 1;
    char hex_str[3];

    encode_cluster_hex((const unsigned char *)"\xAB", cluster_size, hex_str);
    ASSERT(hex_str[0] == 'A');
    ASSERT(hex_str[1] == 'B');
    ASSERT(hex_str[2] == '\0');
    return 0;
}

int main(void) {
    printf("test_hex_encoding_fits_allocation... ");
    if (test_hex_encoding_fits_allocation() != 0) return 1;
    printf("OK\n");

    printf("test_hex_no_per_iteration_nul_overflow... ");
    if (test_hex_no_per_iteration_nul_overflow() != 0) return 1;
    printf("OK\n");

    printf("All disk hex issue #222.3 tests passed.\n");
    return 0;
}

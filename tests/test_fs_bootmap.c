/* Boot memory map to arena selection, bootinfo validation and the KeyQuorum heap. */
#include "bootinfo.h"
#include "bootmap.h"
#include "kq_heap.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define MIB (1ull << 20)
#define CAP FL_BOOTINFO_E820_CAP
#define E(b, l, t) { (b), (l), (t), FL_E820_ATTR_ENABLED }

static const fl_bootmap_limits_t k_limits = { MIB, 1ull << 30, 40 * MIB, 4096 };

static int pick(const fl_e820_entry_t *e, uint32_t n, const fl_boot_range_t *r, uint32_t nr,
                const fl_bootmap_limits_t *lim, uint64_t *base, uint64_t *size)
{
    *base = *size = 0xdeadull;
    return fl_bootmap_select_arena(e, n, CAP, r, nr, lim, base, size);
}

/* QEMU -m 64M as SeaBIOS reports it, plus the loader's own reservations below 1 MiB. */
static const fl_e820_entry_t k_qemu64[] = {
    E(0, 0x9fc00, 1), E(0x9fc00, 0x400, 2), E(0xf0000, 0x10000, 2),
    E(0x100000, 0x3ee0000, 1), E(0x3fe0000, 0x20000, 2), E(0xfffc0000, 0x40000, 2),
};
static const fl_boot_range_t k_low_res[] = { { 0, 0x500 }, { 0x500, 0x1000 }, { 0x10000, 0x9b000 } };

static int test_qemu_map(void)
{
    uint64_t b, s;

    ASSERT(pick(k_qemu64, 6, k_low_res, 3, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == 0x100000 && s == 0x3ee0000);
    {
        fl_e820_entry_t small[6];

        memcpy(small, k_qemu64, sizeof small);
        small[3].length = 0x1ee0000;
        ASSERT(pick(small, 6, k_low_res, 3, &k_limits, &b, &s) == FL_BOOTMAP_E_NOFIT);
        ASSERT(b == 0xdeadull && s == 0xdeadull);
    }
    return 0;
}

static int test_unsorted_same_answer(void)
{
    const fl_e820_entry_t shuffled[] = {
        k_qemu64[5], k_qemu64[3], k_qemu64[0], k_qemu64[4], k_qemu64[2], k_qemu64[1],
    };
    uint64_t b, s;

    ASSERT(pick(shuffled, 6, k_low_res, 3, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == 0x100000 && s == 0x3ee0000);
    return 0;
}

static int test_overlapping_usable_join(void)
{
    const fl_e820_entry_t e[] = { E(8 * MIB, 40 * MIB, 1), E(MIB, 9 * MIB, 1), E(48 * MIB, 4 * MIB, 1) };
    uint64_t b, s;

    ASSERT(pick(e, 3, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB && s == 51 * MIB);
    return 0;
}

static int test_reserved_entry_wins_overlap(void)
{
    const fl_e820_entry_t e[] = { E(30 * MIB, MIB, 2), E(MIB, 63 * MIB, 1) };
    fl_bootmap_limits_t lim = k_limits;
    uint64_t b, s;

    lim.min_size = 20 * MIB;
    ASSERT(pick(e, 2, NULL, 0, &lim, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == 31 * MIB && s == 33 * MIB); /* [31, 64) beats [1, 30) */
    ASSERT(b >= 31 * MIB || b + s <= 30 * MIB);
    return 0;
}

static int test_reservation_splits_region(void)
{
    const fl_e820_entry_t e[] = { E(MIB, 63 * MIB, 1) };
    const fl_boot_range_t mid[] = { { 25 * MIB, 26 * MIB } };
    const fl_boot_range_t kernel[] = { { MIB, 2 * MIB } };
    uint64_t b, s;

    ASSERT(pick(e, 1, mid, 1, &k_limits, &b, &s) == FL_BOOTMAP_E_NOFIT);
    ASSERT(pick(e, 1, kernel, 1, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == 2 * MIB && s == 62 * MIB);
    return 0;
}

static int test_overflow_rejected(void)
{
    fl_e820_entry_t e[] = { E(MIB, 63 * MIB, 1), E(UINT64_MAX - 0xfff, 0x2000, 2) };
    uint64_t b, s;

    ASSERT(pick(e, 2, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_OVERFLOW);
    e[1].length = 0x1000; /* ends exactly at 2^64: not representable, still an overflow */
    ASSERT(pick(e, 2, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_OVERFLOW);
    e[1].length = 0xfff; /* ends at 2^64 - 1: fine */
    ASSERT(pick(e, 2, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB && s == 63 * MIB);
    return 0;
}

static int test_zero_length_and_disabled_ignored(void)
{
    fl_e820_entry_t e[] = {
        E(MIB, 63 * MIB, 1), E(10 * MIB, 0, 2), /* zero-length reserved: blocks nothing */
        E(70 * MIB, 0, 1),                      /* zero-length usable: adds nothing */
        { 20 * MIB, MIB, 2, 0 },                /* disabled reserved: ignored */
        { 100 * MIB, 200 * MIB, 1, 0 },         /* disabled usable: ignored */
    };
    uint64_t b, s;

    ASSERT(pick(e, 5, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB && s == 63 * MIB);
    ASSERT(pick(e + 1, 4, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_NOFIT);
    return 0;
}

static int test_truncated_and_invalid(void)
{
    fl_boot_range_t bad[] = { { 5 * MIB, 5 * MIB } };
    fl_bootmap_limits_t lim = k_limits;
    uint64_t b, s;

    ASSERT(pick(k_qemu64, 0, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_TRUNCATED);
    ASSERT(fl_bootmap_select_arena(k_qemu64, 6, 5, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_TRUNCATED);
    ASSERT(pick(k_qemu64, 6, bad, 1, &k_limits, &b, &s) == FL_BOOTMAP_E_INVAL);
    bad[0].end = 4 * MIB;
    ASSERT(pick(k_qemu64, 6, bad, 1, &k_limits, &b, &s) == FL_BOOTMAP_E_INVAL);
    ASSERT(pick(k_qemu64, 6, NULL, 1, &k_limits, &b, &s) == FL_BOOTMAP_E_INVAL);
    ASSERT(pick(NULL, 6, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_INVAL);
    ASSERT(fl_bootmap_select_arena(k_qemu64, 6, CAP, NULL, 0, &k_limits, NULL, &s) == FL_BOOTMAP_E_INVAL);
    lim.align = 3000;
    ASSERT(pick(k_qemu64, 6, NULL, 0, &lim, &b, &s) == FL_BOOTMAP_E_INVAL);
    lim.align = 0;
    ASSERT(pick(k_qemu64, 6, NULL, 0, &lim, &b, &s) == FL_BOOTMAP_E_INVAL);
    lim = k_limits;
    lim.min_base = lim.max_end;
    ASSERT(pick(k_qemu64, 6, NULL, 0, &lim, &b, &s) == FL_BOOTMAP_E_INVAL);
    return 0;
}

static int test_alignment_and_map_limit(void)
{
    const fl_e820_entry_t e[] = { E(MIB + 1, 4ull << 30, 1) };
    uint64_t b, s;

    ASSERT(pick(e, 1, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB + 4096 && s == (1ull << 30) - b);
    ASSERT((b & 4095) == 0 && b + s <= (1ull << 30));
    return 0;
}

static int test_below_min_base_excluded(void)
{
    const fl_e820_entry_t e[] = { E(0, 0x9fc00, 1), E(2ull << 30, 1ull << 30, 1) };
    fl_bootmap_limits_t lim = k_limits;
    uint64_t b, s;

    lim.min_size = 1;
    ASSERT(pick(e, 2, NULL, 0, &lim, &b, &s) == FL_BOOTMAP_E_NOFIT);
    return 0;
}


#define K_START 0x10000ull
#define K_IMAGE 0x82118ull /* the loaded bytes end here, below FL_LOW_BUDGET */
#define K_BSS 0x100000ull  /* FL_KERNEL_BSS_ADDR: never loaded */
#define K_END 0x115320ull

static struct {
    fl_boot_info_t bi;
    fl_e820_entry_t map[FL_BOOTINFO_E820_CAP];
} s_low;

/* Stage 2's handoff layout; the boot-info block sits in a host buffer standing in for 0x510-0x1000. */
static void good_info(fl_boot_info_t *bi)
{
    memset(&s_low, 0, sizeof s_low);
    memcpy(s_low.map, k_qemu64, sizeof k_qemu64);
    bi->magic = FL_BOOTINFO_MAGIC;
    bi->version = FL_BOOTINFO_VERSION;
    bi->size = sizeof *bi;
    bi->boot_drive = 0x80;
    bi->e820_addr = (uintptr_t)s_low.map;
    bi->e820_count = 6;
    bi->e820_capacity = FL_BOOTINFO_E820_CAP;
    bi->kernel_base = K_START;
    bi->kernel_load_end = K_START + 913 * 512;
    bi->kernel_end = K_END;
    bi->mapped_end = 1ull << 30;
    bi->reserved_count = 8;
    bi->reserved[0] = (fl_boot_range_t){ 0, FL_BOOTINFO_ADDR };
    bi->reserved[1] = (fl_boot_range_t){ (uintptr_t)&s_low, (uintptr_t)&s_low + sizeof s_low };
    bi->reserved[2] = (fl_boot_range_t){ FL_PAGE_TABLES, FL_PAGE_TABLES_END };
    bi->reserved[3] = (fl_boot_range_t){ 0x7000, 0x8e00 };
    bi->reserved[4] = (fl_boot_range_t){ FL_KERNEL_STACK_BOTTOM, FL_KERNEL_STACK_TOP };
    bi->reserved[5] = (fl_boot_range_t){ K_START, K_START + 913 * 512 };
    bi->reserved[6] = (fl_boot_range_t){ 0xa0000, 0x100000 };
    bi->reserved[7] = (fl_boot_range_t){ K_BSS, K_END };
}

static int validate(const fl_boot_info_t *bi)
{
    return fl_bootinfo_validate(bi, K_START, K_IMAGE, K_BSS, K_END);
}

static int test_bootinfo_validate(void)
{
#define bi (s_low.bi)

    good_info(&bi);
    ASSERT(validate(&bi) == FL_BOOTMAP_OK);
    ASSERT(fl_bootinfo_validate(NULL, K_START, K_IMAGE, K_BSS, K_END) == FL_BOOTMAP_E_INVAL);
    ASSERT(fl_bootinfo_validate(&bi, K_START, K_IMAGE, 0x9f000, K_END) == FL_BOOTMAP_E_INVAL); /* bss below 1 MiB */
    ASSERT(fl_bootinfo_validate(&bi, K_START, K_BSS + 512, K_BSS, K_END) == FL_BOOTMAP_E_INVAL); /* image runs into it */
#define BAD(field_change, want) do { good_info(&bi); field_change; ASSERT(validate(&bi) == (want)); } while (0)
    BAD(bi.magic ^= 1, FL_BOOTMAP_E_INVAL);
    BAD(bi.version = 2, FL_BOOTMAP_E_INVAL);
    BAD(bi.size -= 8, FL_BOOTMAP_E_INVAL);
    BAD(bi.flags = 2, FL_BOOTMAP_E_INVAL);
    BAD(bi.flags = FL_BOOTINFO_F_E820_TRUNCATED, FL_BOOTMAP_E_TRUNCATED);
    BAD(bi.e820_count = 0, FL_BOOTMAP_E_TRUNCATED);
    BAD(bi.e820_count = FL_BOOTINFO_E820_CAP + 1, FL_BOOTMAP_E_TRUNCATED);
    BAD((bi.e820_capacity = FL_BOOTINFO_E820_CAP + 1, bi.e820_count = 6), FL_BOOTMAP_E_INVAL);
    BAD(bi.e820_addr = 0, FL_BOOTMAP_E_INVAL);
    BAD(bi.e820_addr += 4, FL_BOOTMAP_E_INVAL);
    BAD(bi.e820_addr = UINT64_MAX - 7, FL_BOOTMAP_E_INVAL);           /* buffer end wraps */
    BAD(bi.kernel_base = K_START + 0x1000, FL_BOOTMAP_E_INVAL);       /* not this image */
    BAD(bi.kernel_end = K_END + 16, FL_BOOTMAP_E_INVAL);
    BAD(bi.kernel_load_end = K_START + 912 * 512, FL_BOOTMAP_E_INVAL); /* image not fully read */
    BAD(bi.kernel_load_end = K_BSS + 512, FL_BOOTMAP_E_INVAL);         /* loader wrote into the bss */
    BAD(bi.kernel_load_end += 1, FL_BOOTMAP_E_INVAL);                  /* not whole sectors */
    BAD(bi.mapped_end = K_END - 1, FL_BOOTMAP_E_INVAL);
    BAD(bi.reserved_count = 0, FL_BOOTMAP_E_INVAL);
    BAD(bi.reserved_count = FL_BOOTINFO_MAX_RESERVED + 1, FL_BOOTMAP_E_INVAL);
    BAD(bi.reserved[5].end -= 512, FL_BOOTMAP_E_INVAL);                /* image not reserved */
    BAD(bi.reserved[7].end = K_END - 1, FL_BOOTMAP_E_INVAL);           /* bss not reserved */
    BAD(s_low.map[3].length = K_END - K_BSS - 1, FL_BOOTMAP_E_INVAL);   /* bss not all RAM */
    BAD(s_low.map[3].type = 2, FL_BOOTMAP_E_INVAL);                     /* bss over reserved memory */
    BAD((s_low.map[5] = (fl_e820_entry_t){ 0x110000, 0x1000, 2, 1 }), FL_BOOTMAP_E_INVAL); /* a hole in the bss */
    BAD(bi.reserved[4].base += 16, FL_BOOTMAP_E_INVAL);                /* stack not reserved */
    BAD(bi.reserved[2].end -= 0x1000, FL_BOOTMAP_E_INVAL);             /* page tables not reserved */
    BAD(bi.reserved[1].end -= 24, FL_BOOTMAP_E_INVAL);                 /* E820 buffer not reserved */
    BAD(bi.reserved[1].base += 8, FL_BOOTMAP_E_INVAL);                 /* boot info not reserved */
    BAD(bi.reserved[7] = ((fl_boot_range_t){ 9, 9 }), FL_BOOTMAP_E_INVAL);
    BAD(bi.e820_count = FL_BOOTINFO_E820_CAP, FL_BOOTMAP_OK);
#undef BAD
    good_info(&bi);
    {
        uint64_t b, s;

        ASSERT(pick((const fl_e820_entry_t *)(uintptr_t)bi.e820_addr, bi.e820_count, bi.reserved,
                    bi.reserved_count, &k_limits, &b, &s) == FL_BOOTMAP_OK);
        ASSERT(b == 0x116000 && s == 0x3fe0000 - 0x116000); /* past the bss, page aligned */
    }
#undef bi
    return 0;
}

/* bootinfo.inc is the loader's copy of the layout: every value must match the C side. */
static int test_inc_matches_header(const char *path)
{
    static const struct { const char *name; unsigned long long want; } k[] = {
        { "FL_BOOTINFO_MAGIC", FL_BOOTINFO_MAGIC },
        { "FL_BOOTINFO_VERSION", FL_BOOTINFO_VERSION },
        { "FL_BOOTINFO_SIZE", sizeof(fl_boot_info_t) },
        { "FL_BOOTINFO_ADDR", FL_BOOTINFO_ADDR },
        { "FL_BOOTINFO_E820_ADDR", FL_BOOTINFO_E820_ADDR },
        { "FL_BOOTINFO_E820_CAP", FL_BOOTINFO_E820_CAP },
        { "FL_BOOTINFO_MAX_RESERVED", FL_BOOTINFO_MAX_RESERVED },
        { "FL_BOOTINFO_F_E820_TRUNCATED", FL_BOOTINFO_F_E820_TRUNCATED },
        { "FL_PAGE_TABLES", FL_PAGE_TABLES },
        { "FL_PAGE_TABLES_END", FL_PAGE_TABLES_END },
        { "FL_KERNEL_STACK_BOTTOM", FL_KERNEL_STACK_BOTTOM },
        { "FL_KERNEL_STACK_TOP", FL_KERNEL_STACK_TOP },
        { "FL_KERNEL_BSS_ADDR", FL_KERNEL_BSS_ADDR },
        { "BI_MAGIC", offsetof(fl_boot_info_t, magic) },
        { "BI_VERSION", offsetof(fl_boot_info_t, version) },
        { "BI_SIZE", offsetof(fl_boot_info_t, size) },
        { "BI_FLAGS", offsetof(fl_boot_info_t, flags) },
        { "BI_BOOT_DRIVE", offsetof(fl_boot_info_t, boot_drive) },
        { "BI_E820_ADDR", offsetof(fl_boot_info_t, e820_addr) },
        { "BI_E820_COUNT", offsetof(fl_boot_info_t, e820_count) },
        { "BI_E820_CAPACITY", offsetof(fl_boot_info_t, e820_capacity) },
        { "BI_KERNEL_BASE", offsetof(fl_boot_info_t, kernel_base) },
        { "BI_KERNEL_LOAD_END", offsetof(fl_boot_info_t, kernel_load_end) },
        { "BI_KERNEL_END", offsetof(fl_boot_info_t, kernel_end) },
        { "BI_MAPPED_END", offsetof(fl_boot_info_t, mapped_end) },
        { "BI_RESERVED_COUNT", offsetof(fl_boot_info_t, reserved_count) },
        { "BI_RESERVED", offsetof(fl_boot_info_t, reserved) },
    };
    char line[256];
    unsigned found = 0;
    FILE *f = fopen(path, "r");

    ASSERT(f != NULL);
    ASSERT(sizeof(fl_e820_entry_t) == 24 && sizeof(fl_boot_range_t) == 16);
    while (fgets(line, sizeof line, f)) {
        char name[64];
        char value[64];

        if (sscanf(line, "%%define %63s %63s", name, value) != 2)
            continue;
        for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
            if (strcmp(name, k[i].name) == 0) {
                if (strtoull(value, NULL, 0) != k[i].want) {
                    fprintf(stderr, "FAIL: %s is %s in %s, %#llx in bootinfo.h\n", name, value, path, k[i].want);
                    fclose(f);
                    return 1;
                }
                found++;
            }
    }
    fclose(f);
    ASSERT(found == sizeof k / sizeof k[0]);
    return 0;
}

static int test_min_size_boundary(void)
{
    fl_e820_entry_t e[] = { E(MIB, 40 * MIB, 1) };
    uint64_t b, s;

    ASSERT(pick(e, 1, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK); /* exactly FL_KQ_HEAP_MIN */
    ASSERT(b == MIB && s == 40 * MIB && s == FL_KQ_HEAP_MIN);
    e[0].length -= 1;
    ASSERT(pick(e, 1, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_NOFIT);
    return 0;
}

static int test_full_buffer(void)
{
    static fl_e820_entry_t e[CAP];
    uint64_t b, s;

    for (uint32_t i = 0; i + 1 < CAP; i++)
        e[i] = (fl_e820_entry_t)E(MIB + i * 2 * 4096, 4096, (i & 1) ? 2u : 1u);
    e[CAP - 1] = (fl_e820_entry_t)E(64 * MIB, 48 * MIB, 1);
    ASSERT(fl_bootmap_select_arena(e, CAP, CAP, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == 64 * MIB && s == 48 * MIB);
    ASSERT(fl_bootmap_select_arena(e, CAP, CAP - 1, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_E_TRUNCATED);
    return 0;
}

static int test_equal_spans_lowest_wins(void)
{
    const fl_e820_entry_t e[] = { E(200 * MIB, 40 * MIB, 1), E(MIB, 40 * MIB, 1), E(100 * MIB, 40 * MIB, 1) };
    uint64_t b, s;

    ASSERT(pick(e, 3, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB && s == 40 * MIB);
    return 0;
}

static int test_other_types_block(void)
{
    const uint32_t types[] = { 3, 4, 5, 12 };

    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        const fl_e820_entry_t e[] = { E(MIB, 63 * MIB, 1), E(20 * MIB, MIB, types[i]) };
        uint64_t b, s;

        ASSERT(pick(e, 2, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
        ASSERT(b == 21 * MIB && s == 43 * MIB);
    }
    return 0;
}


static int all_zero(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

static int test_kq_heap(void)
{
    const size_t arena_size = FL_KQ_HEAP_MIN + MIB;
    unsigned char *arena = aligned_alloc(4096, arena_size);
    unsigned char *a, *c, *big;

    ASSERT(arena != NULL);
    memset(arena, 0x5a, arena_size);
    ASSERT(fl_kq_heap_alloc(16, 16) == NULL); /* no arena yet: fail closed */
    ASSERT(fl_kq_heap_init((uintptr_t)arena + 8, arena_size - 8) == -1);           /* misaligned */
    ASSERT(fl_kq_heap_init((uintptr_t)arena, FL_KQ_HEAP_MIN - 1) == -1);          /* too small */
    ASSERT(fl_kq_heap_init(UINT64_MAX - 15, FL_KQ_HEAP_MIN) == -1);               /* wraps */
    ASSERT(fl_kq_heap_alloc(16, 16) == NULL);
    ASSERT(fl_kq_heap_init((uintptr_t)arena, arena_size) == 0);
    ASSERT(fl_kq_heap_init((uintptr_t)arena, arena_size) == -1);                  /* only once */

    ASSERT(fl_kq_heap_alloc(16, 24) == NULL && fl_kq_heap_alloc(16, 8192) == NULL); /* not a power of two; over 4096 */
    ASSERT(fl_kq_heap_alloc(arena_size + 1, 16) == NULL);
    a = fl_kq_heap_alloc(1000, 4096);
    ASSERT(a && ((uintptr_t)a & 4095) == 0 && a >= arena && a + 1000 <= arena + arena_size);
    ASSERT(fl_kq_heap_usable(a) >= 1000);
    c = fl_kq_heap_alloc(300, 16);
    ASSERT(c && ((uintptr_t)c & 15) == 0 && (c >= a + 1000 || c + 300 <= a));
    memset(a, 0xab, 1000);
    memset(c, 0xcd, 300);
    fl_kq_heap_free(a);
    ASSERT(all_zero(a, 1000));       /* freed memory is wiped */
    ASSERT(c[0] == 0xcd && c[299] == 0xcd);
    fl_kq_heap_free(c);
    ASSERT(all_zero(c, 300));
    fl_kq_heap_free(NULL);

    a = fl_kq_heap_alloc(20 * MIB, 16);
    c = fl_kq_heap_alloc(20 * MIB, 16);
    ASSERT(a && c && fl_kq_heap_alloc(2 * MIB, 16) == NULL);
    fl_kq_heap_free(a);
    fl_kq_heap_free(c);
    big = fl_kq_heap_alloc(FL_KQ_HEAP_MIN, 16);
    ASSERT(big != NULL);
    fl_kq_heap_free(big);
    free(arena);
    return 0;
}

int main(int argc, char **argv)
{
    int (*const tests[])(void) = {
        test_qemu_map, test_unsorted_same_answer, test_overlapping_usable_join,
        test_reserved_entry_wins_overlap, test_reservation_splits_region, test_overflow_rejected,
        test_zero_length_and_disabled_ignored, test_truncated_and_invalid,
        test_alignment_and_map_limit, test_below_min_base_excluded, test_min_size_boundary,
        test_full_buffer, test_equal_spans_lowest_wins, test_other_types_block, test_kq_heap,
    };

    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++)
        if (tests[i]())
            return 1;
    if (test_bootinfo_validate() ||
        test_inc_matches_header(argc > 1 ? argv[1] : "kernel/freestanding/x86_64/bootinfo.inc"))
        return 1;
    puts("test_fs_bootmap: PASS");
    return 0;
}

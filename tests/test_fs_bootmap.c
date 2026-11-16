/*
 * Freestanding boot memory map (FL-02): fl_bootmap_select_arena picks the KeyQuorum heap arena
 * from an E820 map. Covers QEMU's map, unsorted and overlapping entries, reservations that split
 * a usable region, overflowing, zero-length and disabled entries, truncated maps, alignment and
 * the identity-map limit.
 */
#include "bootmap.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define MIB (1ull << 20)
#define CAP 64u
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
    /* 32 MiB of RAM cannot hold a 40 MiB arena: refused, nothing written. */
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

    /* [1, 10) and [8, 48) overlap, [48, 52) touches: one span [1, 52) MiB. */
    ASSERT(pick(e, 3, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB && s == 51 * MIB);
    return 0;
}

static int test_reserved_entry_wins_overlap(void)
{
    /* The reserved entry is listed first and overlaps the usable one: it is never in the arena. */
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

    /* Split into 24 + 38 MiB: neither half holds 40 MiB, and the halves are never joined. */
    ASSERT(pick(e, 1, mid, 1, &k_limits, &b, &s) == FL_BOOTMAP_E_NOFIT);
    /* A kernel loaded at 1 MiB pushes the arena above it. */
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
    /* Only zero-length or disabled memory: nothing to use. */
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

    /* Starts on 4 KiB and stops at the 1 GiB identity map. */
    ASSERT(pick(e, 1, NULL, 0, &k_limits, &b, &s) == FL_BOOTMAP_OK);
    ASSERT(b == MIB + 4096 && s == (1ull << 30) - b);
    ASSERT((b & 4095) == 0 && b + s <= (1ull << 30));
    return 0;
}

static int test_below_min_base_excluded(void)
{
    /* All usable memory below 1 MiB (or above the map limit): nothing for the arena. */
    const fl_e820_entry_t e[] = { E(0, 0x9fc00, 1), E(2ull << 30, 1ull << 30, 1) };
    fl_bootmap_limits_t lim = k_limits;
    uint64_t b, s;

    lim.min_size = 1;
    ASSERT(pick(e, 2, NULL, 0, &lim, &b, &s) == FL_BOOTMAP_E_NOFIT);
    return 0;
}

int main(void)
{
    int (*const tests[])(void) = {
        test_qemu_map, test_unsorted_same_answer, test_overlapping_usable_join,
        test_reserved_entry_wins_overlap, test_reservation_splits_region, test_overflow_rejected,
        test_zero_length_and_disabled_ignored, test_truncated_and_invalid,
        test_alignment_and_map_limit, test_below_min_base_excluded,
    };

    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++)
        if (tests[i]())
            return 1;
    puts("test_fs_bootmap: PASS");
    return 0;
}

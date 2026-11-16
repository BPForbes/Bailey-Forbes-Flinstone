/*
 * Boot memory map: see bootmap.h for the rules. No sorting and no scratch memory: every
 * candidate start (min_base, each usable entry's base, each blocker's end) is grown through the
 * usable entries that touch it and then cut at the first blocker after it. With the buffer's
 * bounded entry count that is a few hundred thousand comparisons at most, once per boot.
 */
#include "bootinfo.h"
#include "bootmap.h"

enum { KIND_IGNORED, KIND_USABLE, KIND_BLOCKER };

static int entry_kind(const fl_e820_entry_t *e)
{
    if (e->length == 0 || !(e->attr & FL_E820_ATTR_ENABLED))
        return KIND_IGNORED;
    return e->type == FL_E820_USABLE ? KIND_USABLE : KIND_BLOCKER;
}

/* The largest end such that [start, end) lies in usable entries, no blocker touches it and it
 * stays below max_end; start itself when there is none. */
static uint64_t span_end(const fl_e820_entry_t *entries, uint32_t count,
                         const fl_boot_range_t *res, uint32_t nres, uint64_t start, uint64_t max_end)
{
    uint64_t end = start;
    int grew = 1;

    for (uint32_t pass = 0; grew && pass <= count; pass++) {
        grew = 0;
        for (uint32_t i = 0; i < count; i++) {
            const fl_e820_entry_t *e = &entries[i];

            if (entry_kind(e) == KIND_USABLE && e->base <= end && end < e->base + e->length) {
                end = e->base + e->length;
                grew = 1;
            }
        }
    }
    for (uint32_t i = 0; i < count + nres; i++) {
        uint64_t b, x;

        if (i < count) {
            if (entry_kind(&entries[i]) != KIND_BLOCKER)
                continue;
            b = entries[i].base;
            x = b + entries[i].length;
        } else {
            b = res[i - count].base;
            x = res[i - count].end;
        }
        if (b < end && x > start)
            end = b <= start ? start : b;
    }
    return end < max_end ? end : max_end;
}

int fl_bootmap_select_arena(const fl_e820_entry_t *entries, uint32_t count, uint32_t capacity,
                            const fl_boot_range_t *reservations, uint32_t reservation_count,
                            const fl_bootmap_limits_t *limits, uint64_t *out_base, uint64_t *out_size)
{
    uint64_t best_base = 0, best_size = 0;

    if (!entries || !limits || !out_base || !out_size || (reservation_count && !reservations))
        return FL_BOOTMAP_E_INVAL;
    if (!limits->align || (limits->align & (limits->align - 1)) || limits->min_base >= limits->max_end)
        return FL_BOOTMAP_E_INVAL;
    if (count == 0 || count > capacity)
        return FL_BOOTMAP_E_TRUNCATED;
    for (uint32_t i = 0; i < count; i++)
        if (entries[i].length > UINT64_MAX - entries[i].base)
            return FL_BOOTMAP_E_OVERFLOW;
    for (uint32_t i = 0; i < reservation_count; i++)
        if (reservations[i].base >= reservations[i].end)
            return FL_BOOTMAP_E_INVAL;

    /* Candidate starts: index 0 is min_base, then one per entry, then one per reservation. */
    for (uint32_t c = 0; c <= count + reservation_count; c++) {
        uint64_t s, end;

        if (c == 0) {
            s = limits->min_base;
        } else if (c <= count) {
            const fl_e820_entry_t *e = &entries[c - 1];
            int kind = entry_kind(e);

            if (kind == KIND_IGNORED)
                continue;
            s = kind == KIND_USABLE ? e->base : e->base + e->length;
        } else {
            s = reservations[c - 1 - count].end;
        }
        if (s < limits->min_base)
            s = limits->min_base;
        if (s > UINT64_MAX - (limits->align - 1))
            continue;
        s = (s + limits->align - 1) & ~(limits->align - 1);
        if (s >= limits->max_end)
            continue;
        end = span_end(entries, count, reservations, reservation_count, s, limits->max_end);
        if (end - s > best_size || (end - s == best_size && best_size && s < best_base)) {
            best_base = s;
            best_size = end - s;
        }
    }
    if (best_size == 0 || best_size < limits->min_size)
        return FL_BOOTMAP_E_NOFIT;
    *out_base = best_base;
    *out_size = best_size;
    return FL_BOOTMAP_OK;
}

/* [base, end) lies inside one of the loader's reservations. */
static int reserved_covers(const fl_boot_info_t *bi, uint64_t base, uint64_t end)
{
    for (uint32_t i = 0; i < bi->reserved_count; i++)
        if (bi->reserved[i].base <= base && end <= bi->reserved[i].end)
            return 1;
    return 0;
}

int fl_bootinfo_validate(const fl_boot_info_t *bi, uint64_t kernel_start, uint64_t bss_start, uint64_t kernel_end)
{
    uint64_t map_end;

    if (!bi || bi->magic != FL_BOOTINFO_MAGIC || bi->version != FL_BOOTINFO_VERSION ||
        bi->size != sizeof(*bi) || (bi->flags & ~FL_BOOTINFO_F_E820_TRUNCATED))
        return FL_BOOTMAP_E_INVAL;
    if ((bi->flags & FL_BOOTINFO_F_E820_TRUNCATED) || bi->e820_count == 0 ||
        bi->e820_count > bi->e820_capacity)
        return FL_BOOTMAP_E_TRUNCATED;
    if (bi->e820_capacity > FL_BOOTINFO_E820_CAP || !bi->e820_addr || (bi->e820_addr & 7))
        return FL_BOOTMAP_E_INVAL;
    map_end = bi->e820_addr + (uint64_t)bi->e820_capacity * sizeof(fl_e820_entry_t);
    /* The image the loader describes is this image: same start, same end, fully read. */
    if (bi->kernel_base != kernel_start || bi->kernel_end != kernel_end || kernel_start >= bss_start ||
        bss_start > kernel_end || bi->kernel_load_end < bss_start ||
        ((bi->kernel_load_end - kernel_start) & 511) || bi->mapped_end < kernel_end)
        return FL_BOOTMAP_E_INVAL;
    if (bi->reserved_count == 0 || bi->reserved_count > FL_BOOTINFO_MAX_RESERVED)
        return FL_BOOTMAP_E_INVAL;
    for (uint32_t i = 0; i < bi->reserved_count; i++)
        if (bi->reserved[i].base >= bi->reserved[i].end)
            return FL_BOOTMAP_E_INVAL;
    /* The image, its stack, the page tables, the E820 buffer and this struct are reserved: no
     * arena can take them. */
    if (map_end < bi->e820_addr || !reserved_covers(bi, kernel_start, kernel_end) ||
        !reserved_covers(bi, FL_KERNEL_STACK_BOTTOM, FL_KERNEL_STACK_TOP) ||
        !reserved_covers(bi, FL_PAGE_TABLES, FL_PAGE_TABLES_END) ||
        !reserved_covers(bi, bi->e820_addr, map_end) ||
        !reserved_covers(bi, (uintptr_t)bi, (uintptr_t)bi + sizeof(*bi)))
        return FL_BOOTMAP_E_INVAL;
    return FL_BOOTMAP_OK;
}

#ifndef FL_PMM_H
#define FL_PMM_H

#include "contract_result.h"
#include <stddef.h>
#include <stdint.h>

/* Physical frame size matches the virtual page size. */
#define PMM_FRAME_SIZE  4096u

/* Tracked physical memory: 4 MB, matching the ramdisk declaration.
 * Physical frames start at PHYS_BASE (1 MB) to skip the legacy BIOS region. */
#define PMM_PHYS_BASE   0x100000UL
#define PMM_PHYS_MEM    (4u * 1024u * 1024u)
#define PMM_NUM_FRAMES  (PMM_PHYS_MEM / PMM_FRAME_SIZE)  /* 1024 */

/* Reserve a physical address range by marking all covered frames as allocated.
 * Call during early boot (before any allocations) to protect kernel/reserved regions. */
void pmm_reserve_range(uintptr_t base, size_t bytes);

/* Allocate one 4 KiB physical frame.
 * Returns the physical address of the frame, or 0 on exhaustion. */
uintptr_t pmm_alloc_frame(void);

/* P0-2 / P1-4: fallible alloc; *out_phys set on FL_RESULT_OK. */
fl_result_t pmm_alloc_frame_result(uintptr_t *out_phys);

/* Return a previously allocated frame to the free pool. */
void pmm_free_frame(uintptr_t phys_addr);

fl_result_t pmm_free_frame_result(uintptr_t phys_addr);

/* Return the number of currently free frames. */
size_t pmm_free_count(void);

#endif /* FL_PMM_H */

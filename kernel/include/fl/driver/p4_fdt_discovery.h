/**
 * **P4-6** FDT-driven machine discovery (lab DTB walk).
 */
#ifndef FL_P4_FDT_DISCOVERY_H
#define FL_P4_FDT_DISCOVERY_H

#include <stddef.h>
#include <stdint.h>
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_P4_FDT_MAGIC 0xD00DFEEDu

typedef struct {
    int has_memory;
    int has_cpus;
    int has_psci;
    int compatible_count;
    char compatible_last[64];
} fl_p4_fdt_summary_t;

fl_result_t fl_p4_fdt_walk(const void *dtb, size_t dtb_len, fl_p4_fdt_summary_t *summary);

/** Resolve **aliases** node: copy target path for **name** into **out** (NUL-terminated). */
fl_result_t fl_p4_fdt_resolve_alias(const void *dtb, size_t dtb_len, const char *name,
                                    char *out, size_t out_cap);

/** Write a minimal valid DTB for lab tests; returns byte length or 0 on **cap** too small. */
size_t fl_p4_fdt_write_minimal_lab(uint8_t *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_FDT_DISCOVERY_H */

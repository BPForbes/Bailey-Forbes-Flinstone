/**
 * **P4-7** PSCI client (status words → **fl_result_t**).
 */
#ifndef FL_P4_PSCI_H
#define FL_P4_PSCI_H

#include <stddef.h>
#include <stdint.h>
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_PSCI_STATUS_SUCCESS         0
#define FL_PSCI_STATUS_NOT_SUPPORTED   (-1)
#define FL_PSCI_STATUS_INVALID_PARAMS  (-2)
#define FL_PSCI_STATUS_DENIED          (-3)
#define FL_PSCI_STATUS_ALREADY_ON      (-4)

fl_result_t fl_psci_status_to_result(int32_t psci_status);

fl_result_t fl_psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id);
fl_result_t fl_psci_cpu_off(void);
fl_result_t fl_psci_cpu_suspend(uint32_t power_state);

/** Discover **psci** node from DTB via **P4-6** walk. */
fl_result_t fl_psci_discover_from_dtb(const void *dtb, size_t dtb_len);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_PSCI_H */

#include "fl/driver/p4_psci.h"
#include "fl/driver/p4_fdt_discovery.h"

static int s_psci_present;

fl_result_t fl_psci_status_to_result(int32_t psci_status) {
    switch (psci_status) {
    case FL_PSCI_STATUS_SUCCESS:
        return FL_RESULT_OK;
    case FL_PSCI_STATUS_NOT_SUPPORTED:
        return FL_RESULT_NOSYS;
    case FL_PSCI_STATUS_INVALID_PARAMS:
        return FL_RESULT_INVAL;
    case FL_PSCI_STATUS_DENIED:
        return FL_RESULT_ACCES;
    case FL_PSCI_STATUS_ALREADY_ON:
        return FL_RESULT_BUSY;
    default:
        return FL_RESULT_ERR;
    }
}

fl_result_t fl_psci_discover_from_dtb(const void *dtb, size_t dtb_len) {
    fl_p4_fdt_summary_t sum;
    fl_result_t rc = fl_p4_fdt_walk(dtb, dtb_len, &sum);
    if (rc != FL_RESULT_OK)
        return rc;
    s_psci_present = sum.has_psci;
    return sum.has_psci ? FL_RESULT_OK : FL_RESULT_NOENT;
}

fl_result_t fl_psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id) {
    (void)target_cpu;
    (void)entry_point;
    (void)context_id;
    if (!s_psci_present)
        return FL_RESULT_NOENT;
#if defined(__aarch64__) && defined(DRIVERS_BAREMETAL)
    /* Lab SMC path would invoke **FL_CONTRACT_P4_PSCI_CPU_ON** here. */
    return fl_psci_status_to_result(FL_PSCI_STATUS_SUCCESS);
#else
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_psci_cpu_off(void) {
    if (!s_psci_present)
        return FL_RESULT_NOENT;
#if defined(__aarch64__) && defined(DRIVERS_BAREMETAL)
    return fl_psci_status_to_result(FL_PSCI_STATUS_SUCCESS);
#else
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_psci_cpu_suspend(uint32_t power_state) {
    (void)power_state;
    if (!s_psci_present)
        return FL_RESULT_NOENT;
#if defined(__aarch64__) && defined(DRIVERS_BAREMETAL)
    return fl_psci_status_to_result(FL_PSCI_STATUS_SUCCESS);
#else
    return FL_RESULT_NOSYS;
#endif
}

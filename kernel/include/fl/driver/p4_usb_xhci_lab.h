/**
 * **P4-5** xHCI lab subset (register groups, TRB ordering, event-ring BH).
 */
#ifndef FL_P4_USB_XHCI_LAB_H
#define FL_P4_USB_XHCI_LAB_H

#include <stdint.h>
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_XHCI_REG_CAP_CAPLENGTH 0x00u
#define FL_XHCI_REG_CAP_HCIVERSION 0x02u
#define FL_XHCI_REG_OP_USBCMD 0x00u
#define FL_XHCI_REG_OP_USBSTS 0x04u
#define FL_XHCI_REG_RT_IR_IMAN0 0x20u
#define FL_XHCI_DB_TARGET 0x00u

typedef struct {
    uint32_t parameter;
    uint32_t status;
    uint32_t control;
} fl_xhci_trb_t;

typedef struct {
    volatile void *mmio_base;
    uint8_t cap_length;
    uint32_t op_base;
    uint32_t rt_base;
    uint32_t db_base;
} fl_xhci_register_map_t;

fl_result_t fl_xhci_lab_map_registers(volatile void *mmio_base, fl_xhci_register_map_t *out);

uint32_t fl_xhci_lab_read_cap(volatile void *mmio_base, uint8_t cap_offset);
uint32_t fl_xhci_lab_read_op(volatile const fl_xhci_register_map_t *map, uint8_t op_offset);
void     fl_xhci_lab_write_op(volatile const fl_xhci_register_map_t *map, uint8_t op_offset,
                              uint32_t value);

/**
 * Prepare TRB for ring publish: set fields then **Cycle** bit last (Intel xHCI §4.9).
 * Calls **fl_usb_xhci_mmio_rw_fence** before returning.
 */
fl_result_t fl_xhci_lab_trb_publish(fl_xhci_trb_t *trb, uint64_t buffer_ptr, uint32_t trb_type,
                                    int cycle_bit);

/** Schedule event-ring service on the **P4-2** bottom-half path. */
fl_result_t fl_xhci_lab_event_ring_schedule_bh(int irq);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_USB_XHCI_LAB_H */

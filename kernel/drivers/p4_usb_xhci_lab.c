#include "fl/driver/p4_usb_xhci_lab.h"
#include "fl/driver/p4_irq_lifecycle.h"
#include "fl/driver/usb_xhci_mmio_glue.h"
#include <stddef.h>

static void xhci_event_bh(int irq, void *ctx) {
    (void)irq;
    (void)ctx;
    /* Lab: event-ring drain would run here (blocking allowed in BH). */
}

fl_result_t fl_xhci_lab_map_registers(volatile void *mmio_base, fl_xhci_register_map_t *out) {
    if (!mmio_base || !out)
        return FL_RESULT_INVAL;
    uint8_t caplen =
        (uint8_t)(fl_usb_xhci_mmio_read32_volatile(mmio_base) & 0xFFu);
    if (caplen < 0x20u)
        return FL_RESULT_INVAL;
    out->mmio_base = mmio_base;
    out->cap_length = caplen;
    out->op_base = caplen;
    out->rt_base = caplen + 0x400u; /* lab layout: runtime block after op window */
    out->db_base = caplen + 0x800u;
    return FL_RESULT_OK;
}

uint32_t fl_xhci_lab_read_cap(volatile void *mmio_base, uint8_t cap_offset) {
    return fl_usb_xhci_mmio_read32_volatile((volatile uint8_t *)mmio_base + cap_offset);
}

uint32_t fl_xhci_lab_read_op(volatile const fl_xhci_register_map_t *map, uint8_t op_offset) {
    if (!map || !map->mmio_base)
        return 0;
    return fl_usb_xhci_mmio_read32_volatile((volatile uint8_t *)map->mmio_base + map->op_base +
                                            op_offset);
}

void fl_xhci_lab_write_op(volatile const fl_xhci_register_map_t *map, uint8_t op_offset,
                          uint32_t value) {
    if (!map || !map->mmio_base)
        return;
    fl_usb_xhci_mmio_write32_volatile((volatile uint8_t *)map->mmio_base + map->op_base +
                                      op_offset, value);
}

fl_result_t fl_xhci_lab_trb_publish(fl_xhci_trb_t *trb, uint64_t buffer_ptr, uint32_t trb_type,
                                    int cycle_bit) {
    if (!trb)
        return FL_RESULT_INVAL;
    trb->parameter = (uint32_t)(buffer_ptr & 0xFFFFFFFFu);
    trb->status = (uint32_t)(buffer_ptr >> 32);
    trb->control = (trb_type & 0x3Fu) << 10;
    fl_usb_xhci_mmio_rw_fence();
    trb->control |= cycle_bit ? 1u : 0u;
    fl_usb_xhci_mmio_rw_fence();
    return FL_RESULT_OK;
}

fl_result_t fl_xhci_lab_event_ring_schedule_bh(int irq) {
    if (fl_irq_lifecycle_defer_bottom_half(irq, xhci_event_bh, NULL) != 0)
        return FL_RESULT_BUSY;
    return FL_RESULT_OK;
}

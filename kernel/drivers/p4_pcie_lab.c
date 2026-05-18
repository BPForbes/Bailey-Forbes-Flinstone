#include "fl/driver/p4_pcie_lab.h"

int fl_pcie_lab_iommu_bypass_assumed(void) {
    return 1;
}

fl_result_t fl_pcie_lab_read_config_dword(uint8_t bus, uint8_t dev, uint8_t fn,
                                          uint8_t offset, uint32_t *out) {
    if (!out)
        return FL_RESULT_INVAL;
    *out = pci_read_config32(bus, dev, fn, offset);
    return FL_RESULT_OK;
}

fl_result_t fl_pcie_lab_write_config_dword(uint8_t bus, uint8_t dev, uint8_t fn,
                                           uint8_t offset, uint32_t value) {
    pci_write_config32(bus, dev, fn, offset, value);
    return FL_RESULT_OK;
}

fl_result_t fl_pcie_lab_decode_bar(const pci_config_header_t *hdr, unsigned bar_index,
                                   fl_pcie_bar_info_t *out) {
    if (!hdr || !out || bar_index >= 6u)
        return FL_RESULT_INVAL;
    uint32_t raw = hdr->bar[bar_index];
    if (raw == 0u || raw == 0xFFFFFFFFu)
        return FL_RESULT_NOENT;
    out->is_64bit = (raw & 0x4u) != 0u;
    /* PCI BAR bit 0: 0 = memory (MMIO), 1 = I/O ports. */
    out->is_mmio = (raw & 0x1u) == 0u;
    if (out->is_64bit) {
        uint64_t lo = (uint64_t)(raw & ~0xFu);
        uint64_t hi = (uint64_t)hdr->bar[bar_index + 1];
        out->phys = lo | (hi << 32);
        out->size = 0; /* sizing requires write-all-ones probe — lab uses decoded base only */
    } else {
        out->phys = (uint64_t)(raw & ~0xFu);
        out->size = 0;
    }
    return FL_RESULT_OK;
}

static int pci_find_capability(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t cap_id,
                               uint8_t *cap_off_out) {
    uint8_t status = pci_read_config8(bus, dev, fn, 0x06);
    if (!(status & 0x10u))
        return -1;
    uint8_t ptr = pci_read_config8(bus, dev, fn, 0x34) & 0xFCu;
    for (int guard = 0; ptr && guard < 48; guard++) {
        uint8_t id = pci_read_config8(bus, dev, fn, ptr);
        if (id == cap_id) {
            *cap_off_out = ptr;
            return 0;
        }
        ptr = pci_read_config8(bus, dev, fn, (uint8_t)(ptr + 1)) & 0xFCu;
    }
    return -1;
}

fl_result_t fl_pcie_lab_setup_msi(uint8_t bus, uint8_t dev, uint8_t fn, unsigned vector) {
    uint8_t off = 0;
    if (pci_find_capability(bus, dev, fn, 0x05u, &off) != 0)
        return FL_RESULT_NOENT;
    /* MSI message control: enable MSI (bit 0) and set vector in low bits where supported. */
    uint16_t mc = pci_read_config16(bus, dev, fn, (uint8_t)(off + 2));
    mc = (uint16_t)((mc & 0xFF8Fu) | 0x0001u | ((vector & 0x1Fu) << 4));
    pci_write_config16(bus, dev, fn, (uint8_t)(off + 2), mc);
    return FL_RESULT_OK;
}

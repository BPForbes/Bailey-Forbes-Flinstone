/**
 * Unified I/O stack for drivers. Tier order: 1 (preferred) -> 3 (legacy).
 *
 * Tier 1 - MMIO (mmio.h): AHCI, NVMe, USB, APIC, HPET, GOP. Works on x86 & ARM.
 * Tier 1 - PCI config (pci.h): Device discovery. x86: port 0xCF8/0xCFC or ECAM; ARM: ECAM.
 * Tier 2 - Port I/O (port_io.h): PS/2 (0x60/0x64), PIC (0x20/0xA0), PIT (0x40). x86 only; ARM stubs.
 * Tier 3 - Legacy: VGA ports, serial, IDE. Niche.
 */
#ifndef IO_H
#define IO_H

#include "port_io.h"
#include "mmio.h"
#include "pci.h"

#endif /* IO_H */

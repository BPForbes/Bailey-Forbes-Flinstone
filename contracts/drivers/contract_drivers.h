/*
 * P4 driver / hardware-facing contract bundle (contracts/drivers).
 *
 * Include this header when implementing or reviewing **P4-1** … **P4-7** work. Build with
 * **-Icontracts/drivers** alongside the other per-contract **-I** include roots.
 *
 * **Layering:** this bundle extends **contract_extend.h** (P0–P1 vocabulary). It does **not**
 * pull **contract_networking.h** — **P3** netdev and datapath stay in the **networking** contract tree.
 * Translation units that need both should include both headers explicitly.
 *
 * Bump **FL_CONTRACT_P4_DRIVERS_REV** when the shard set or mandatory **P4** vocabulary changes.
 */
#ifndef FL_CONTRACT_DRIVERS_H
#define FL_CONTRACT_DRIVERS_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_DRIVERS_REV 4

#include "contract_p4_driver_model.h"
#include "contract_p4_irq_lifecycle.h"
#include "contract_p4_pcie_lab.h"
#include "contract_p4_virtio.h"
#include "contract_p4_usb.h"
#include "contract_p4_fdt_discovery.h"
#include "contract_p4_psci.h"
#include "contract_p4_kworker.h"

#define FL_CONTRACT_P4_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P4_VOCABULARY_LOCK == 1,
               "P4 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_DRIVERS_H */

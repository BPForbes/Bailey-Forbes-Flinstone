/* **P8-2 — Guest virtio** (module contract, normative).
 *
 * **Distribution:** **Guest-visible MMIO** windows, **doorbell / notify** paths, and **DMA
 * visibility** rules for virtio devices follow **Virtio 1.x** guest expectations. **Descriptor
 * rings, feature bits, and transport setup** remain **P4-4** (**`contracts/drivers/`**,
 * **`contract_p4_virtio.h`**); **P8** records **guest-side ownership** of guest RAM views and
 * **notification ordering** relative to **P8-1** replay timelines—not a second copy of ring
 * layout prose.
 *
 * **Outcomes:** negotiation or map failures return **`fl_result_t`** (**P0-2**).
 */
#ifndef FL_CONTRACT_P8_VIRTIO_GUEST_H
#define FL_CONTRACT_P8_VIRTIO_GUEST_H

#include "contract_extend.h"

#define FL_CONTRACT_P8_2_VIRTIO_GUEST_CONTRACT_DEFINED 1

/** Guest RAM backing virtqueues remains guest-owned until the device consumes published chains. */
#define FL_CONTRACT_P8_GUEST_OWNS_VIRTQUEUE_RAM 1

/** Include contract_p4_virtio.h only in TUs that program rings (P4 owns barrier rules). */
#define FL_CONTRACT_P8_VIRTIO_RING_RULES_IN_P4 1

_Static_assert(FL_CONTRACT_P8_GUEST_OWNS_VIRTQUEUE_RAM == 1,
               "guest virtio contract must keep RAM ownership explicit");

#endif /* FL_CONTRACT_P8_VIRTIO_GUEST_H */

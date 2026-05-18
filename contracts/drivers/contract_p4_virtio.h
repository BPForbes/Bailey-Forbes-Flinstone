/**
 * **P4-4 — Virtio net/block** (module contract, normative).
 *
 * **Distribution:** **virtqueue** descriptors are **owned** by the driver during **inflight**
 * **TX/RX** or **block** operations; the device may **consume** chains only after **memory
 * barrier** rules from **Virtio 1.x** are satisfied. **Feature negotiation** is append-only
 * per transport session; **golden-vector** tests validate ring layout independently of
 * silicon (**VM** track).
 *
 * **Outcomes:** ring setup and feature negotiation return **`fl_result_t`** in
 * **FL_RESULT_MIN** … **FL_RESULT_MAX** (**P0-2**).
 */
#ifndef FL_CONTRACT_P4_VIRTIO_H
#define FL_CONTRACT_P4_VIRTIO_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_4_VIRTIO_CONTRACT_DEFINED 1

/** Descriptor publish must issue a full memory barrier before notifying the device. */
#define FL_CONTRACT_P4_4_DESC_PUBLISH_BARRIER_REQUIRED 1

#endif /* FL_CONTRACT_P4_VIRTIO_H */

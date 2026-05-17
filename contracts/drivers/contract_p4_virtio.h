/**
 * **P4-4 — Virtio net/block** (module contract, normative).
 *
 * **Distribution:** **virtqueue** descriptors are **owned** by the driver during **inflight**
 * **TX/RX** or **block** operations; the device may **consume** chains only after **memory
 * barrier** rules from **Virtio 1.x** are satisfied. **Feature negotiation** is append-only
 * per transport session; **golden-vector** tests validate ring layout independently of
 * silicon (**VM** track).
 */
#ifndef FL_CONTRACT_P4_VIRTIO_H
#define FL_CONTRACT_P4_VIRTIO_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_4_VIRTIO_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_VIRTIO_H */

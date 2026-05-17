/**
 * **P3-1 — Device abstraction (`netdev`)** (module contract, normative).
 *
 * Obligations:
 *   - **L2 interchange** is **IEEE 802.3 Ethernet** frames (addressing, MTU vs frame size).
 *   - Expose **TX/RX** frame paths, **MAC** get/set where applicable, **promiscuous** mode
 *     as an explicit capability (deny-by-default on **H** unless elevated per **P2**),
 *     and **stats** counters with defined overflow/wrap behaviour or saturation.
 *   - **Default** path is **copy-based**; **zero-copy** is optional and must be named in
 *     implementation reviews when introduced.
 *   - Align public driver ops with **kernel/include/fl/driver/net.h** and phase gates in
 *     **docs/ROADMAP.md** Phase 3.
 *
 * See **docs/ROADMAP.md** Phase 3; anchor for driver and stack glue reviews.
 */
#ifndef FL_CONTRACT_P3_NETDEV_H
#define FL_CONTRACT_P3_NETDEV_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_1_NETDEV_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_NETDEV_H */

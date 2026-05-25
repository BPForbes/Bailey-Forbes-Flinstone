/**
 * **P3-14 — Network stack background jobs** (module contract, normative).
 *
 * **Distribution:** deferred net work (RX dequeue, TCP timers, ARP TTL sweep) is
 * kicked on **P1-8** **`fl_wq_default()`**, not on the shell **threadpool** MLQ.
 * Tags identify the net subsystem slice; default layer **FL_WQ_LAYER_BACKGROUND**
 * unless documented otherwise (see contracts/runtime/contract_p1_workqueue.h).
 *
 * **Depends:** **P3-1** netdev, **P3-7** TCP (timers), **P1-7**, **P1-8**.
 */
#ifndef FL_CONTRACT_P3_BACKGROUND_H
#define FL_CONTRACT_P3_BACKGROUND_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_14_BACKGROUND_CONTRACT_DEFINED 1

/** **fl_wq_work_t.tag** for ARP cache TTL / sweep kicks (**#240**). */
#define FL_NET_BG_TAG_ARP_TICK 0x0314u

/** Hub tag alias when routing through **fl_bg_jobs_tick**. */
#define FL_BG_JOB_TAG_NET FL_NET_BG_TAG_ARP_TICK

#endif /* FL_CONTRACT_P3_BACKGROUND_H */

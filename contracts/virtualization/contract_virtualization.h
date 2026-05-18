/*
 * P8 virtualization / guest fidelity contract bundle (contracts/virtualization).
 *
 * Include this header (or individual contract_p8_*.h after contract_extend.h) when
 * implementing or reviewing **P8-1** device timing fidelity, **P8-2** guest virtio vocabulary,
 * and **P8-3** QEMU-class lab machine profiles. Build with -Icontracts/virtualization alongside
 * other contract bundle -I flags.
 *
 * Layering: extends contract_extend.h (P0 vocabulary). It does **not** include
 * contract_drivers.h — pull **P4-4** virtio ring rules only where a TU programs rings.
 *
 * Bump FL_CONTRACT_P8_VIRTUALIZATION_REV when shard set or mandatory P8 vocabulary changes.
 */
#ifndef FL_CONTRACT_VIRTUALIZATION_H
#define FL_CONTRACT_VIRTUALIZATION_H

#include "contract_extend.h"

#define FL_CONTRACT_P8_VIRTUALIZATION_REV 1

#include "contract_p8_timing.h"
#include "contract_p8_virtio_guest.h"
#include "contract_p8_qemu_lab.h"

#define FL_CONTRACT_P8_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P8_VOCABULARY_LOCK == 1,
               "P8 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_VIRTUALIZATION_H */

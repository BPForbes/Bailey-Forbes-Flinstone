/**
 * **P4-4** Virtio 1.x ring helpers (memory barriers + golden-vector layout checks).
 */
#ifndef FL_P4_VIRTIO_H
#define FL_P4_VIRTIO_H

#include <stddef.h>
#include <stdint.h>
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_VIRTIO_RING_SIZE_GOLDEN 2u

typedef struct {
    uint64_t desc_addr;
    uint64_t avail_addr;
    uint64_t used_addr;
    uint16_t queue_size;
} fl_virtio_split_layout_t;

void fl_virtio_desc_publish_barrier(void);

/** Golden-vector: validate split virtqueue layout for queue_size **2**. */
fl_result_t fl_virtio_ring_golden_vector_validate(const fl_virtio_split_layout_t *layout);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_VIRTIO_H */

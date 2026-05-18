#include "fl/driver/p4_virtio.h"

void fl_virtio_desc_publish_barrier(void) {
#if defined(__GNUC__)
    __atomic_thread_fence(__ATOMIC_RELEASE);
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#endif
}

fl_result_t fl_virtio_ring_golden_vector_validate(const fl_virtio_split_layout_t *layout) {
    if (!layout)
        return FL_RESULT_INVAL;
    if (layout->queue_size != FL_VIRTIO_RING_SIZE_GOLDEN)
        return FL_RESULT_INVAL;
    /* Descriptor table: 16 bytes per desc * queue_size */
    const uint64_t desc_bytes = 16u * (uint64_t)layout->queue_size;
    const uint64_t avail_off = layout->desc_addr + desc_bytes;
    const uint64_t used_off = avail_off + 6u + 2u * (uint64_t)layout->queue_size;
    if (layout->avail_addr != avail_off)
        return FL_RESULT_INVAL;
    if (layout->used_addr != used_off)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

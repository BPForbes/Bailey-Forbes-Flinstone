# Kernel build configuration

# Architecture selection
if(TARGET_ARCH STREQUAL "x86_64")
    add_subdirectory(arch/x86_64)
    set(KERNEL_ARCH_LIB kernel_arch_x86_64)
elseif(TARGET_ARCH STREQUAL "aarch64")
    add_subdirectory(arch/aarch64)
    set(KERNEL_ARCH_LIB kernel_arch_aarch64)
else()
    message(FATAL_ERROR "Unsupported architecture: ${TARGET_ARCH}")
endif()

# Core kernel sources (architecture-independent)
set(KERNEL_CORE_SOURCES
    core/sched/priority_queue.c
    core/sched/scheduler.c
    core/sched/task.c
    core/vfs/vfs.c
    core/vfs/path.c
    core/mm/kmalloc.c
    core/mm/paging_common.c
    core/mm/slab.c
    core/ipc/pipe.c
    core/ipc/msgq.c
    core/sys/syscall.c
    core/sys/errno.c
    core/main.c
)

# Create kernel library
add_library(kernel STATIC
    ${KERNEL_CORE_SOURCES}
    $<TARGET_OBJECTS:${KERNEL_ARCH_LIB}>
)

# Include directories
target_include_directories(kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/${TARGET_ARCH}/include
)

# Link libraries
target_link_libraries(kernel PRIVATE
    pthread
)

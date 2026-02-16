# aarch64 architecture-specific build

# Assembly sources (GAS)
set(AARCH64_ASM_SOURCES
    asm/memcpy.S
    asm/alloc_core.S
    asm/alloc_malloc.S
    asm/alloc_free.S
)

# C sources
set(AARCH64_C_SOURCES
    drivers/port_io.c
    mm/paging.c
    mm/mmu.c
    interrupts/vectors.c
    interrupts/gic.c
    drivers/timer.c
    drivers/uart.c
)

# Create object library for aarch64 architecture
add_library(kernel_arch_aarch64 OBJECT
    ${AARCH64_ASM_SOURCES}
    ${AARCH64_C_SOURCES}
)

# Include directories
target_include_directories(kernel_arch_aarch64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kernel/include
)

# Compiler flags
target_compile_options(kernel_arch_aarch64 PRIVATE
    -march=armv8-a
    -mtune=cortex-a57
)

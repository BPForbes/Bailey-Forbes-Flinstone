# x86_64 architecture-specific build

# Assembly sources (NASM)
set(X86_64_ASM_SOURCES
    asm/memcpy.asm
    asm/alloc_core.asm
    asm/alloc_malloc.asm
    asm/alloc_free.asm
)

# C sources
set(X86_64_C_SOURCES
    drivers/port_io.c
    mm/paging.c
    mm/gdt_idt.c
    interrupts/idt.c
    interrupts/pic_apic.c
    drivers/timer.c
    drivers/keyboard.c
    drivers/disk.c
)

# Create object library for x86_64 architecture
add_library(kernel_arch_x86_64 OBJECT
    ${X86_64_ASM_SOURCES}
    ${X86_64_C_SOURCES}
)

# Set NASM flags
set_source_files_properties(${X86_64_ASM_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-f elf64"
)

# Include directories
target_include_directories(kernel_arch_x86_64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kernel/include
)

# Compiler flags
target_compile_options(kernel_arch_x86_64 PRIVATE
    -m64
    -march=x86-64
)

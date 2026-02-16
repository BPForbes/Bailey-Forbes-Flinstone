# x86_64 toolchain configuration
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compiler settings
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# NASM assembler for x86_64
find_program(NASM nasm)
if(NASM)
    set(CMAKE_ASM_NASM_COMPILER ${NASM})
    set(CMAKE_ASM_NASM_FLAGS "-f elf64")
    enable_language(ASM_NASM)
else()
    message(WARNING "NASM not found. Assembly files will not be compiled.")
endif()

# Architecture-specific flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64")

# Set target architecture
set(TARGET_ARCH "x86_64" CACHE STRING "Target architecture")

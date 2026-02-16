# aarch64 toolchain configuration
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compiler settings (adjust paths as needed)
if(DEFINED ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER $ENV{CROSS_COMPILE}gcc)
    set(CMAKE_CXX_COMPILER $ENV{CROSS_COMPILE}g++)
    set(CMAKE_ASM_COMPILER $ENV{CROSS_COMPILE}as)
else()
    # Try to find aarch64-linux-gnu toolchain
    find_program(AARCH64_GCC aarch64-linux-gnu-gcc)
    if(AARCH64_GCC)
        set(CMAKE_C_COMPILER ${AARCH64_GCC})
        get_filename_component(TOOLCHAIN_BIN ${AARCH64_GCC} DIRECTORY)
        set(CMAKE_CXX_COMPILER ${TOOLCHAIN_BIN}/aarch64-linux-gnu-g++)
        set(CMAKE_ASM_COMPILER ${TOOLCHAIN_BIN}/aarch64-linux-gnu-as)
    else()
        message(WARNING "aarch64 cross-compiler not found. Using host compiler.")
        set(CMAKE_C_COMPILER gcc)
        set(CMAKE_CXX_COMPILER g++)
        set(CMAKE_ASM_COMPILER as)
    endif()
endif()

# Architecture-specific flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")

# Set target architecture
set(TARGET_ARCH "aarch64" CACHE STRING "Target architecture")

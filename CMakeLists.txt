cmake_minimum_required(VERSION 3.15)
project(Bailey-Forbes-Flinstone VERSION 1.0.0 LANGUAGES C ASM)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Architecture detection
if(NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()

# Default to x86_64 if not specified
if(NOT TARGET_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(TARGET_ARCH "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(TARGET_ARCH "aarch64")
    else()
        set(TARGET_ARCH "x86_64")
    endif()
endif()

message(STATUS "Building for architecture: ${TARGET_ARCH}")

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

# Include toolchain if specified
if(CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_TOOLCHAIN_FILE})
endif()

# Include custom modules
include(DetectWSL)
include(NASM)
include(Warnings)

# Set architecture-specific paths
set(KERNEL_ARCH_DIR "${CMAKE_SOURCE_DIR}/kernel/arch/${TARGET_ARCH}")
set(KERNEL_CORE_DIR "${CMAKE_SOURCE_DIR}/kernel/core")
set(KERNEL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/kernel/include")

# Add include directories
include_directories(
    ${KERNEL_INCLUDE_DIR}
    ${KERNEL_ARCH_DIR}/include
)

# Build kernel
add_subdirectory(kernel)

# Build userland
add_subdirectory(userland)

# Build VM (optional)
option(BUILD_VM "Build VM harness" OFF)
if(BUILD_VM)
    add_subdirectory(vm)
endif()

# Build tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.15)

# Shipped semver: read from the same generated header as the shell (version/entries → gen_version_def.sh).
# Do not hardcode A.B.C here — it would drift on every release bump.
set(_FLINT_VERSION_DEF "${CMAKE_SOURCE_DIR}/userland/shell/version_def.h")
if(NOT EXISTS "${_FLINT_VERSION_DEF}")
  message(FATAL_ERROR "Missing ${_FLINT_VERSION_DEF}. Run ./scripts/gen_version_def.sh or `make` after updating version/entries.")
endif()
file(READ "${_FLINT_VERSION_DEF}" _flint_version_def_h)
string(REGEX MATCH "#define[ \t]+VERSION_MAJOR[ \t]+([0-9]+)" _ "${_flint_version_def_h}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Could not parse VERSION_MAJOR from ${_FLINT_VERSION_DEF}")
endif()
set(_FLINT_VMAJ "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+VERSION_STANDARD[ \t]+([0-9]+)" _ "${_flint_version_def_h}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Could not parse VERSION_STANDARD from ${_FLINT_VERSION_DEF}")
endif()
set(_FLINT_VSTD "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+VERSION_PATCH[ \t]+([0-9]+)" _ "${_flint_version_def_h}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Could not parse VERSION_PATCH from ${_FLINT_VERSION_DEF}")
endif()
set(_FLINT_VPT "${CMAKE_MATCH_1}")

project(Bailey-Forbes-Flinstone VERSION ${_FLINT_VMAJ}.${_FLINT_VSTD}.${_FLINT_VPT} LANGUAGES C ASM)

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
    ${CMAKE_SOURCE_DIR}/contracts/foundations
    ${CMAKE_SOURCE_DIR}/contracts/runtime
    ${CMAKE_SOURCE_DIR}/contracts/identity
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

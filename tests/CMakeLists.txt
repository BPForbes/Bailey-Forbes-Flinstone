# Tests build configuration

# Find CUnit
find_package(CUnit QUIET)
if(NOT CUnit_FOUND)
    message(WARNING "CUnit not found. Tests will not be built.")
    return()
endif()

set(TEST_SOURCES
    kernel_tests.c
    pq_tests.c
    vfs_tests.c
)

add_executable(tests ${TEST_SOURCES})

target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/include
)

target_link_libraries(tests PRIVATE
    kernel
    CUnit::CUnit
)

# Run tests with CTest
enable_testing()
add_test(NAME AllTests COMMAND tests)

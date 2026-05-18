# Shell build configuration

set(SHELL_SOURCES
    sh.c
    builtins.c
    sudo.c
)

add_executable(shell ${SHELL_SOURCES})

target_include_directories(shell PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/contracts/foundations
    ${CMAKE_SOURCE_DIR}/contracts/runtime
    ${CMAKE_SOURCE_DIR}/contracts/identity
    ${CMAKE_SOURCE_DIR}/contracts/storage
    ${CMAKE_SOURCE_DIR}/contracts/observability
    ${CMAKE_SOURCE_DIR}/contracts/operations
)

target_link_libraries(shell PRIVATE
    kernel
)

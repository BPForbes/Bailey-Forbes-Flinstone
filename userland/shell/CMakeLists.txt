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
)

target_link_libraries(shell PRIVATE
    kernel
)

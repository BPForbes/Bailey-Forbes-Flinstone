# VM harness build configuration

set(VM_SOURCES
    host/main.c
    host/input.c
    devices/virt_disk.c
    devices/virt_net.c
    devices/virt_timer.c
)

# Platform-specific window implementations
if(WIN32)
    list(APPEND VM_SOURCES host/window_win32.c)
elseif(UNIX)
    list(APPEND VM_SOURCES host/window_x11.c)
endif()

add_executable(vm_harness ${VM_SOURCES})

target_include_directories(vm_harness PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/include
)

# SDL2 for windowing (optional)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    target_link_libraries(vm_harness PRIVATE SDL2::SDL2)
    target_compile_definitions(vm_harness PRIVATE VM_SDL=1)
endif()

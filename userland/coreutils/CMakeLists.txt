# Coreutils build configuration

set(COREUTILS_SOURCES
    ls.c
    cat.c
    mkdir.c
    rm.c
)

# Create individual executables for each coreutil
foreach(util ${COREUTILS_SOURCES})
    get_filename_component(util_name ${util} NAME_WE)
    add_executable(${util_name} ${util})
    target_include_directories(${util_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/kernel/include
        ${CMAKE_SOURCE_DIR}/contracts/foundations
        ${CMAKE_SOURCE_DIR}/contracts/runtime
        ${CMAKE_SOURCE_DIR}/contracts/identity
        ${CMAKE_SOURCE_DIR}/contracts/storage
        ${CMAKE_SOURCE_DIR}/contracts/observability
    )
    target_link_libraries(${util_name} PRIVATE kernel)
endforeach()

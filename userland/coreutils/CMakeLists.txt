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
    )
    target_link_libraries(${util_name} PRIVATE kernel)
endforeach()

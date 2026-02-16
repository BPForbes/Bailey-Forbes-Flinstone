# Warnings.cmake - Configure compiler warnings

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Common warnings
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    # Additional warnings for GCC
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
            add_compile_options(-Wcast-align -Wcast-qual -Wformat=2)
            add_compile_options(-Wlogical-op -Wmissing-declarations)
            add_compile_options(-Wredundant-decls -Wstrict-prototypes)
        endif()
    endif()
    
    # Additional warnings for Clang
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-Wdocumentation -Wmissing-prototypes)
    endif()
endif()

# MSVC warnings
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4)
endif()

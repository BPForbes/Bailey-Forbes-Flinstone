# NASM.cmake - Find and configure NASM assembler

find_program(NASM_EXECUTABLE
    nasm
    PATHS
        /usr/bin
        /usr/local/bin
        /opt/local/bin
        ENV PATH
)

if(NASM_EXECUTABLE)
    message(STATUS "Found NASM: ${NASM_EXECUTABLE}")
    
    # Test NASM version
    execute_process(
        COMMAND ${NASM_EXECUTABLE} -v
        OUTPUT_VARIABLE NASM_VERSION_OUTPUT
        ERROR_VARIABLE NASM_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    set(NASM_FOUND TRUE)
    set(CMAKE_ASM_NASM_COMPILER ${NASM_EXECUTABLE})
    set(CMAKE_ASM_NASM_FLAGS "-f elf64")
else()
    message(WARNING "NASM not found. x86_64 assembly files will not be compiled.")
    set(NASM_FOUND FALSE)
endif()

# Enable NASM language if found
if(NASM_FOUND)
    enable_language(ASM_NASM)
endif()

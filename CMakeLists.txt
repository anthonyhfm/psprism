cmake_minimum_required(VERSION 3.20)
project(psprecomp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(psprecomp
    src/main.cpp
    src/elf.cpp
    src/emitter.cpp
)
target_include_directories(psprecomp PRIVATE src include)
target_compile_options(psprecomp PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
    add_executable(runtime_tests tests/runtime_tests.cpp)
    target_include_directories(runtime_tests PRIVATE include)
    target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME runtime_tests COMMAND runtime_tests)

    find_program(PSP_GCC psp-gcc HINTS "$ENV{PSPDEV}/bin")
    find_program(PSP_LD psp-ld HINTS "$ENV{PSPDEV}/bin")
    find_program(PSP_GXX psp-g++ HINTS "$ENV{PSPDEV}/bin")
    if(PSP_GCC AND PSP_LD AND PSP_GXX)
        add_test(
            NAME psp_cpp_psp_roundtrip
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip.sh
                    $<TARGET_FILE:psprecomp>
                    ${PSP_GCC}
                    ${PSP_LD}
                    ${PSP_GXX}
                    ${CMAKE_CXX_COMPILER}
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()

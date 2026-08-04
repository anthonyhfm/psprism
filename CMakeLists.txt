cmake_minimum_required(VERSION 3.20)
project(psprecomp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(psprecomp
    src/main.cpp
    src/tui.cpp
    src/decrypt.cpp
    src/elf.cpp
    src/emitter.cpp
    src/iso.cpp
    src/project.cpp
)
add_library(psprecomp_ftxui STATIC third_party/ftxui/ftxui.cpp)
target_include_directories(psprecomp_ftxui PUBLIC third_party/ftxui)
find_package(Threads REQUIRED)
target_link_libraries(psprecomp_ftxui PUBLIC Threads::Threads)
target_compile_definitions(psprecomp PRIVATE
    PSPRECOMP_SOURCE_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/include"
    PSPRECOMP_PSPRISM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/psprism")
target_include_directories(psprecomp PRIVATE src include)
if(MSVC)
    target_compile_options(psprecomp PRIVATE /W4 /WX)
else()
    target_compile_options(psprecomp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_link_libraries(psprecomp PRIVATE psprecomp_ftxui ${CMAKE_DL_LIBS})

include(CTest)
if(BUILD_TESTING)
    add_executable(runtime_tests tests/runtime_tests.cpp)
    target_include_directories(runtime_tests PRIVATE include)
    if(MSVC)
        target_compile_options(runtime_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME runtime_tests COMMAND runtime_tests)

    add_executable(iso_tests tests/iso_tests.cpp src/iso.cpp src/decrypt.cpp)
    target_include_directories(iso_tests PRIVATE src)
    if(MSVC)
        target_compile_options(iso_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(iso_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    target_link_libraries(iso_tests PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME iso_tests COMMAND iso_tests)

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

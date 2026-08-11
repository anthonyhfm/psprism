cmake_minimum_required(VERSION 3.20)
project(psprism VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(psprism
    src/main.cpp
    src/decrypt.cpp
    src/elf.cpp
    src/emitter.cpp
    src/iso.cpp
    src/iso_patch.cpp
    src/nids.cpp
    src/project.cpp
)
target_compile_definitions(psprism PRIVATE
    PSPRECOMP_SOURCE_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/include"
    REFRACT_REFRACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/refract")
target_include_directories(psprism PRIVATE src include)
target_compile_options(psprism PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(psprism PRIVATE ${CMAKE_DL_LIBS})

include(CTest)
if(BUILD_TESTING)
    add_executable(runtime_tests tests/runtime_tests.cpp)
    target_include_directories(runtime_tests PRIVATE include)
    target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME runtime_tests COMMAND runtime_tests)

    add_executable(iso_tests tests/iso_tests.cpp src/iso.cpp src/decrypt.cpp)
    target_include_directories(iso_tests PRIVATE src)
    target_compile_options(iso_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(iso_tests PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME iso_tests COMMAND iso_tests)

    add_executable(elf_tests tests/elf_tests.cpp src/elf.cpp)
    target_include_directories(elf_tests PRIVATE src)
    target_compile_options(elf_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME elf_tests COMMAND elf_tests)

    add_executable(refract_utility_tests
        tests/refract_utility_tests.cpp
        refract/src/utility_data.cpp)
    target_include_directories(refract_utility_tests PRIVATE refract/src)
    target_compile_options(refract_utility_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME refract_utility_tests COMMAND refract_utility_tests)

    add_executable(refract_audio_tests tests/refract_audio_tests.cpp)
    target_include_directories(refract_audio_tests PRIVATE
        include
        refract/src)
    target_compile_options(refract_audio_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME refract_audio_tests COMMAND refract_audio_tests)

    find_program(PSP_GCC psp-gcc HINTS "$ENV{PSPDEV}/bin")
    find_program(PSP_LD psp-ld HINTS "$ENV{PSPDEV}/bin")
    find_program(PSP_GXX psp-g++ HINTS "$ENV{PSPDEV}/bin")
    if(PSP_GCC AND PSP_LD AND PSP_GXX)
        add_test(
            NAME psp_cpp_psp_roundtrip
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip.sh
                    $<TARGET_FILE:psprism>
                    ${PSP_GCC}
                    ${PSP_LD}
                    ${PSP_GXX}
                    ${CMAKE_CXX_COMPILER}
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()

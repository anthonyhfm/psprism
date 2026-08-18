cmake_minimum_required(VERSION 3.20)
project(psprism VERSION 0.1.0 LANGUAGES CXX)

option(PSPRECOMP_BUILD_BENCHMARKS "Build deterministic CPU microbenchmarks" OFF)
option(REFRACT_USE_FFMPEG "Enable FFmpeg-backed PSMF/H.264 decoding" ON)

if(REFRACT_USE_FFMPEG)
    set(REFRACT_FFMPEG_HINTS
        /opt/homebrew/opt/ffmpeg
        /usr/local/opt/ffmpeg)
    find_path(REFRACT_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
        HINTS ${REFRACT_FFMPEG_HINTS} PATH_SUFFIXES include REQUIRED)
    foreach(REFRACT_FFMPEG_COMPONENT avcodec avformat avutil swscale)
        string(TOUPPER "${REFRACT_FFMPEG_COMPONENT}" REFRACT_FFMPEG_COMPONENT_UPPER)
        find_library(REFRACT_${REFRACT_FFMPEG_COMPONENT_UPPER}_LIBRARY
            NAMES ${REFRACT_FFMPEG_COMPONENT}
            HINTS ${REFRACT_FFMPEG_HINTS} PATH_SUFFIXES lib REQUIRED)
        list(APPEND REFRACT_FFMPEG_LIBRARIES
            ${REFRACT_${REFRACT_FFMPEG_COMPONENT_UPPER}_LIBRARY})
    endforeach()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(psprism
    src/main.cpp
    src/decrypt.cpp
    src/elf/loader.cpp
    src/elf/code_map.cpp
    src/elf/image.cpp
    src/emitter/relocations.cpp
    src/emitter/format.cpp
    src/emitter/analysis.cpp
    src/emitter/instruction.cpp
    src/emitter/cpp_output.cpp
    src/emitter/project_output.cpp
    src/emitter/coverage.cpp
    src/iso.cpp
    src/iso_patch.cpp
    src/nids.cpp
    src/project/input.cpp
    src/project/templates.cpp
    src/project/iso_patch_template.cpp
    src/project/export.cpp
)
target_compile_definitions(psprism PRIVATE
    PSPRECOMP_SOURCE_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/include"
    REFRACT_REFRACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/refract")
target_include_directories(psprism PRIVATE src include)
target_compile_options(psprism PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(psprism PRIVATE ${CMAKE_DL_LIBS})

if(PSPRECOMP_BUILD_BENCHMARKS)
    add_executable(cpu_benchmarks tests/cpu_benchmarks.cpp)
    target_include_directories(cpu_benchmarks PRIVATE include)
    target_compile_options(cpu_benchmarks PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    set(REFRACT_ATRAC_TEST_SOURCES
        refract/third_party/at3_standalone/atrac.cpp
        refract/third_party/at3_standalone/atrac3.cpp
        refract/third_party/at3_standalone/atrac3plus.cpp
        refract/third_party/at3_standalone/atrac3plusdec.cpp
        refract/third_party/at3_standalone/atrac3plusdsp.cpp
        refract/third_party/at3_standalone/get_bits.cpp
        refract/third_party/at3_standalone/compat.cpp
        refract/third_party/at3_standalone/fft.cpp
        refract/third_party/at3_standalone/mem.cpp)
    add_library(refract_atrac_test STATIC ${REFRACT_ATRAC_TEST_SOURCES})
    target_include_directories(refract_atrac_test PUBLIC
        refract/third_party/at3_standalone)

    add_executable(runtime_tests tests/runtime_tests.cpp)
    target_include_directories(runtime_tests PRIVATE include)
    target_compile_definitions(runtime_tests PRIVATE PSPRECOMP_PROFILE_CPU)
    target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME runtime_tests COMMAND runtime_tests)

    add_executable(iso_tests tests/iso_tests.cpp src/iso.cpp src/decrypt.cpp)
    target_include_directories(iso_tests PRIVATE src)
    target_compile_options(iso_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(iso_tests PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME iso_tests COMMAND iso_tests)

    add_executable(elf_tests
        tests/elf_tests.cpp
        src/elf/loader.cpp
        src/elf/code_map.cpp
        src/elf/image.cpp)
    target_include_directories(elf_tests PRIVATE src)
    target_compile_options(elf_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME elf_tests COMMAND elf_tests)

    add_executable(emitter_tests
        tests/emitter_tests.cpp
        src/emitter/relocations.cpp
        src/emitter/format.cpp
        src/emitter/analysis.cpp
        src/emitter/instruction.cpp
        src/emitter/cpp_output.cpp
        src/emitter/project_output.cpp
        src/emitter/coverage.cpp
        src/elf/loader.cpp
        src/elf/code_map.cpp
        src/elf/image.cpp
        src/nids.cpp)
    target_include_directories(emitter_tests PRIVATE src include)
    target_compile_options(emitter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME emitter_tests COMMAND emitter_tests)

    add_executable(refract_utility_tests
        tests/refract_utility_tests.cpp
        refract/src/utility_data.cpp)
    target_include_directories(refract_utility_tests PRIVATE
        include
        refract/src)
    target_compile_options(refract_utility_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME refract_utility_tests COMMAND refract_utility_tests)

    add_executable(refract_audio_tests
        tests/refract_audio_tests.cpp
        refract/src/host/audio_engine.cpp)
    target_include_directories(refract_audio_tests PRIVATE
        include
        refract/src)
    target_compile_options(refract_audio_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME refract_audio_tests COMMAND refract_audio_tests)

    add_executable(refract_atrac_tests tests/refract_atrac_tests.cpp)
    target_include_directories(refract_atrac_tests PRIVATE
        include
        refract/src)
    target_compile_options(refract_atrac_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(refract_atrac_tests PRIVATE refract_atrac_test)
    add_test(NAME refract_atrac_tests COMMAND refract_atrac_tests)

    add_executable(refract_ge_tests
        tests/refract_ge_tests.cpp
        refract/src/runtime.cpp
        refract/src/utility_data.cpp
        refract/src/stubs/mpeg/media_engine.cpp)
    target_include_directories(refract_ge_tests PRIVATE
        include
        refract/include
        refract/include/pspsdk
        refract/src)
    target_compile_options(refract_ge_tests PRIVATE
        -Wall -Wextra -Wpedantic)
    target_link_libraries(refract_ge_tests PRIVATE refract_atrac_test)
    if(REFRACT_USE_FFMPEG)
        target_include_directories(refract_ge_tests PRIVATE
            ${REFRACT_FFMPEG_INCLUDE_DIR})
        target_compile_definitions(refract_ge_tests PRIVATE
            REFRACT_HAS_FFMPEG=1)
        target_link_libraries(refract_ge_tests PRIVATE
            ${REFRACT_FFMPEG_LIBRARIES})
    endif()
    add_test(NAME refract_ge_tests COMMAND refract_ge_tests)

    add_executable(refract_mpeg_tests
        tests/refract_mpeg_tests.cpp
        refract/src/stubs/mpeg/media_engine.cpp)
    target_include_directories(refract_mpeg_tests PRIVATE
        include
        refract/src)
    target_compile_options(refract_mpeg_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(refract_mpeg_tests PRIVATE refract_atrac_test)
    if(REFRACT_USE_FFMPEG)
        target_include_directories(refract_mpeg_tests PRIVATE
            ${REFRACT_FFMPEG_INCLUDE_DIR})
        target_compile_definitions(refract_mpeg_tests PRIVATE
            REFRACT_HAS_FFMPEG=1)
        target_link_libraries(refract_mpeg_tests PRIVATE
            ${REFRACT_FFMPEG_LIBRARIES})
    endif()
    add_test(NAME refract_mpeg_tests COMMAND refract_mpeg_tests)

    if(APPLE)
        enable_language(OBJCXX)
        add_executable(refract_frontend_tests tests/refract_frontend_tests.mm)
        target_include_directories(refract_frontend_tests PRIVATE
            include
            refract/include
            refract/include/pspsdk
            refract/src)
        target_compile_features(refract_frontend_tests PRIVATE cxx_std_20)
        target_compile_options(refract_frontend_tests PRIVATE
            -Wall -Wextra -Wpedantic -fobjc-arc)
        target_link_libraries(refract_frontend_tests PRIVATE
            "-framework AppKit"
            "-framework GameController"
            "-framework Metal"
            "-framework MetalKit")
        add_test(NAME refract_frontend_tests COMMAND refract_frontend_tests)
    endif()

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

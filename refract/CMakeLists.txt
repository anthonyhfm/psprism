cmake_minimum_required(VERSION 3.20)

enable_language(OBJCXX)

option(REFRACT_DESKTOP_DIALOGS "Build Qt system dialogs for desktop targets" ON)

set(REFRACT_ATRAC_SOURCES
  third_party/at3_standalone/atrac.cpp
  third_party/at3_standalone/atrac3.cpp
  third_party/at3_standalone/atrac3plus.cpp
  third_party/at3_standalone/atrac3plusdec.cpp
  third_party/at3_standalone/atrac3plusdsp.cpp
  third_party/at3_standalone/get_bits.cpp
  third_party/at3_standalone/compat.cpp
  third_party/at3_standalone/fft.cpp
  third_party/at3_standalone/mem.cpp
)

add_library(refract_atrac STATIC ${REFRACT_ATRAC_SOURCES})
target_compile_features(refract_atrac PUBLIC cxx_std_20)
target_include_directories(refract_atrac PUBLIC third_party/at3_standalone)

set(REFRACT_SOURCES
  src/runtime.cpp
  src/utility_data.cpp
  src/psp_sdk_bridge.cpp
  src/host/macos.cpp
  src/host/macos_frontend.mm
)

if(REFRACT_DESKTOP_DIALOGS)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets
    HINTS "/opt/homebrew/opt/qtbase")
  list(APPEND REFRACT_SOURCES src/host/desktop_dialogs.cpp)
endif()

add_library(refract STATIC ${REFRACT_SOURCES})

target_compile_features(refract PUBLIC cxx_std_20)
target_include_directories(refract
  PUBLIC include
  PUBLIC include/pspsdk
  PRIVATE src "${CMAKE_CURRENT_LIST_DIR}/../include"
)
target_compile_options(refract PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(refract PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_link_libraries(refract PUBLIC
  "-framework AppKit"
  "-framework AudioToolbox"
  "-framework GameController"
  "-framework Metal"
  "-framework MetalKit"
)
target_link_libraries(refract PRIVATE refract_atrac)
if(REFRACT_DESKTOP_DIALOGS)
  target_compile_definitions(refract PRIVATE REFRACT_HAS_DESKTOP_DIALOGS=1)
  get_filename_component(REFRACT_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
  target_compile_definitions(refract PRIVATE
    "REFRACT_QT_PLATFORM_PLUGIN_PATH=\"${REFRACT_QT_PREFIX}/share/qt/plugins/platforms\"")
  target_link_libraries(refract PRIVATE Qt6::Widgets)
endif()

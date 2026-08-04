cmake_minimum_required(VERSION 3.20)

enable_language(OBJCXX)

option(PSPRISM_DESKTOP_DIALOGS "Build Qt system dialogs for desktop targets" ON)

set(PSPRISM_SOURCES
  src/runtime.cpp
  src/utility_data.cpp
  src/host/macos.cpp
  src/host/macos_frontend.mm
)

if(PSPRISM_DESKTOP_DIALOGS)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets
    HINTS "/opt/homebrew/opt/qtbase")
  list(APPEND PSPRISM_SOURCES src/host/desktop_dialogs.cpp)
endif()

add_library(psprism STATIC ${PSPRISM_SOURCES})

target_compile_features(psprism PUBLIC cxx_std_20)
target_include_directories(psprism
  PUBLIC include
  PRIVATE src "${CMAKE_CURRENT_LIST_DIR}/../include"
)
target_compile_options(psprism PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(psprism PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_link_libraries(psprism PUBLIC
  "-framework AppKit"
  "-framework GameController"
  "-framework Metal"
  "-framework MetalKit"
)
if(PSPRISM_DESKTOP_DIALOGS)
  target_compile_definitions(psprism PRIVATE PSPRISM_HAS_DESKTOP_DIALOGS=1)
  get_filename_component(PSPRISM_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
  target_compile_definitions(psprism PRIVATE
    "PSPRISM_QT_PLATFORM_PLUGIN_PATH=\"${PSPRISM_QT_PREFIX}/share/qt/plugins/platforms\"")
  target_link_libraries(psprism PRIVATE Qt6::Widgets)
endif()

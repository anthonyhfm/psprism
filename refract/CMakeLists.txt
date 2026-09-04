cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(refract LANGUAGES CXX)
endif()

option(REFRACT_DESKTOP_DIALOGS "Build Qt system dialogs for desktop targets" ON)
option(REFRACT_USE_FFMPEG "Enable FFmpeg-backed PSMF/H.264 decoding" ON)
set(REFRACT_FFMPEG_ROOT "" CACHE PATH
  "Optional FFmpeg installation prefix (containing include, lib and bin)")

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
  src/host/audio_engine.cpp
  src/host/common.cpp
  src/stubs/mpeg/media_engine.cpp
)

if(APPLE)
  enable_language(OBJCXX)
  list(APPEND REFRACT_SOURCES
    src/host/macos.cpp
    src/host/macos_frontend.mm)
  set(REFRACT_PLATFORM_LIBRARIES
    "-framework AppKit"
    "-framework AudioToolbox"
    "-framework GameController"
    "-framework Metal"
    "-framework MetalKit")
elseif(WIN32)
  list(APPEND REFRACT_SOURCES
    src/host/windows.cpp
    src/host/windows_frontend.cpp)
  set(REFRACT_PLATFORM_LIBRARIES
    d3d11
    dxgi
    d3dcompiler
    xaudio2
    xinput9_1_0
    user32
    shell32
    ole32)
else()
  message(FATAL_ERROR
    "Refract has no host backend for ${CMAKE_SYSTEM_NAME}. "
    "Add one under src/host and select it in refract/CMakeLists.txt.")
endif()

if(REFRACT_DESKTOP_DIALOGS)
  set(_REFRACT_QT_HINTS /opt/homebrew/opt/qtbase)
  if(WIN32)
    if(CMAKE_VS_PLATFORM_NAME MATCHES "^[Aa][Rr][Mm]64$")
      file(GLOB _REFRACT_QT_WINDOWS_PREFIXES LIST_DIRECTORIES true
        "C:/Qt/*/msvc*_arm64")
      if(NOT DEFINED QT_HOST_PATH)
        file(GLOB _REFRACT_QT_WINDOWS_HOSTS LIST_DIRECTORIES true
          "C:/Qt/*/msvc*_64")
        list(FILTER _REFRACT_QT_WINDOWS_HOSTS EXCLUDE REGEX "arm64")
        list(SORT _REFRACT_QT_WINDOWS_HOSTS COMPARE NATURAL ORDER DESCENDING)
        if(_REFRACT_QT_WINDOWS_HOSTS)
          list(GET _REFRACT_QT_WINDOWS_HOSTS 0 _REFRACT_QT_WINDOWS_HOST)
          set(QT_HOST_PATH "${_REFRACT_QT_WINDOWS_HOST}" CACHE PATH
            "Qt host tools used by the Windows ARM64 target")
        endif()
      endif()
    else()
      file(GLOB _REFRACT_QT_WINDOWS_PREFIXES LIST_DIRECTORIES true
        "C:/Qt/*/msvc*_64")
      list(FILTER _REFRACT_QT_WINDOWS_PREFIXES EXCLUDE REGEX "arm64")
    endif()
    list(SORT _REFRACT_QT_WINDOWS_PREFIXES
      COMPARE NATURAL ORDER DESCENDING)
    list(APPEND _REFRACT_QT_HINTS ${_REFRACT_QT_WINDOWS_PREFIXES})
  endif()
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets
    HINTS ${_REFRACT_QT_HINTS})
  list(APPEND REFRACT_SOURCES src/host/desktop_dialogs.cpp)
endif()

add_library(refract STATIC ${REFRACT_SOURCES})

target_compile_features(refract PUBLIC cxx_std_20)
target_include_directories(refract
  PUBLIC include
  PUBLIC include/pspsdk
  PRIVATE src "${CMAKE_CURRENT_LIST_DIR}/../include"
)
if(MSVC)
  target_compile_options(refract PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(refract PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(APPLE)
  target_compile_options(refract PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()
target_link_libraries(refract PUBLIC ${REFRACT_PLATFORM_LIBRARIES})
target_link_libraries(refract PRIVATE refract_atrac)
if(REFRACT_USE_FFMPEG)
  set(_REFRACT_FFMPEG_HINTS
    ${REFRACT_FFMPEG_ROOT}
    $ENV{FFMPEG_ROOT}
    /opt/homebrew/opt/ffmpeg
    /usr/local/opt/ffmpeg
    C:/ffmpeg)
  find_path(REFRACT_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS ${_REFRACT_FFMPEG_HINTS} PATH_SUFFIXES include REQUIRED)
  foreach(_library avcodec avformat avutil swscale)
    string(TOUPPER "${_library}" _library_upper)
    find_library(REFRACT_${_library_upper}_LIBRARY NAMES ${_library}
      HINTS ${_REFRACT_FFMPEG_HINTS} PATH_SUFFIXES lib bin REQUIRED)
    target_link_libraries(refract PRIVATE ${REFRACT_${_library_upper}_LIBRARY})
    if(WIN32)
      get_filename_component(_REFRACT_FFMPEG_LIBRARY_DIR
        "${REFRACT_${_library_upper}_LIBRARY}" DIRECTORY)
      get_filename_component(_REFRACT_FFMPEG_PREFIX
        "${_REFRACT_FFMPEG_LIBRARY_DIR}" DIRECTORY)
      file(GLOB _REFRACT_FFMPEG_DLLS CONFIGURE_DEPENDS
        "${_REFRACT_FFMPEG_PREFIX}/bin/av*.dll"
        "${_REFRACT_FFMPEG_PREFIX}/bin/sw*.dll")
      set_property(GLOBAL APPEND PROPERTY REFRACT_WINDOWS_RUNTIME_FILES
        ${_REFRACT_FFMPEG_DLLS})
    endif()
  endforeach()
  target_include_directories(refract PRIVATE ${REFRACT_FFMPEG_INCLUDE_DIR})
  target_compile_definitions(refract PRIVATE REFRACT_HAS_FFMPEG=1)
endif()
if(REFRACT_DESKTOP_DIALOGS)
  target_compile_definitions(refract PRIVATE REFRACT_HAS_DESKTOP_DIALOGS=1)
  get_filename_component(REFRACT_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
  if(WIN32)
    set(REFRACT_QT_PLATFORM_PLUGIN_PATH
      "${REFRACT_QT_PREFIX}/plugins/platforms")
  else()
    set(REFRACT_QT_PLATFORM_PLUGIN_PATH
      "${REFRACT_QT_PREFIX}/share/qt/plugins/platforms")
  endif()
  target_compile_definitions(refract PRIVATE
    "REFRACT_QT_PLATFORM_PLUGIN_PATH=\"${REFRACT_QT_PLATFORM_PLUGIN_PATH}\"")
  target_link_libraries(refract PRIVATE Qt6::Widgets)
endif()

# Generated projects call this after creating their executable. It keeps the
# Windows output self-contained: Qt's platform plugin and all dynamically
# linked FFmpeg libraries are copied beside the game automatically.
function(refract_deploy_runtime target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "refract_deploy_runtime: unknown target ${target}")
  endif()
  if(WIN32 AND REFRACT_DESKTOP_DIALOGS)
    find_program(REFRACT_WINDEPLOYQT_EXECUTABLE NAMES windeployqt
      HINTS "${REFRACT_QT_PREFIX}/bin" REQUIRED)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${REFRACT_WINDEPLOYQT_EXECUTABLE}"
        --no-translations "$<TARGET_FILE:${target}>"
      COMMENT "Deploying Qt runtime for ${target}")
  endif()
  if(WIN32 AND REFRACT_USE_FFMPEG)
    get_property(_REFRACT_RUNTIME_FILES GLOBAL PROPERTY
      REFRACT_WINDOWS_RUNTIME_FILES)
    list(REMOVE_DUPLICATES _REFRACT_RUNTIME_FILES)
    foreach(_runtime_file IN LISTS _REFRACT_RUNTIME_FILES)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_runtime_file}" "$<TARGET_FILE_DIR:${target}>")
    endforeach()
  endif()
endfunction()

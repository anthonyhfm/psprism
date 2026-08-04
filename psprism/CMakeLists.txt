cmake_minimum_required(VERSION 3.20)

add_library(psprism STATIC
  src/runtime.cpp
  src/host/macos.cpp
)

target_compile_features(psprism PUBLIC cxx_std_20)
target_include_directories(psprism
  PUBLIC include
  PRIVATE src "${CMAKE_CURRENT_LIST_DIR}/../include"
)
target_compile_options(psprism PRIVATE -Wall -Wextra -Wpedantic)

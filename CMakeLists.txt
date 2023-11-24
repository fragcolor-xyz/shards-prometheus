project("shards-prometheus")
cmake_minimum_required(VERSION 3.14)
include(ExternalProject)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wall)

#### Files to format and tidy
set(
  MY_PROJECT_SOURCE_FILES
  ${MY_PROJECT_SOURCE_FILES}
  ${CMAKE_CURRENT_LIST_DIR}/prometheus.cpp
  )

#### Header paths for tidy
set(
  MY_PROJECT_HEADER_PATHS
  ${MY_PROJECT_HEADER_PATHS}
  -I${SHARDS_DIR}/include
  -I${SHARDS_DIR}/deps/stb
  -I${SHARDS_DIR}/deps/cpp-taskflow
  -I${SHARDS_DIR}/deps/nameof/include
  -I${SHARDS_DIR}/deps/magic_enum/include
  -I${SHARDS_DIR}/deps/spdlog/include
  )

### setup clang format
find_program(
  CLANG_FORMAT_EXE
  NAMES "clang-format"
  DOC "Path to clang-format executable"
  )
if(NOT CLANG_FORMAT_EXE)
  message(STATUS "clang-format not found.")
else()
  message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")
endif()

#### Format target
if(CLANG_FORMAT_EXE)
  add_custom_target(
    format
    COMMAND ${CLANG_FORMAT_EXE} -i -style=LLVM ${MY_PROJECT_SOURCE_FILES}
    )
endif()
###

### setup clang tidy
find_program(
  CLANG_TIDY_EXE
  NAMES "clang-tidy"
  DOC "Path to clang-tidy executable"
  )
if(NOT CLANG_TIDY_EXE)
  message(STATUS "clang-tidy not found.")
else()
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
endif()

#### tidy target
set(tidyfied)
foreach(_file ${MY_PROJECT_SOURCE_FILES})
  if(CLANG_TIDY_EXE)
    add_custom_command(
      OUTPUT ${_file}.noop
      COMMAND ${CLANG_TIDY_EXE} -checks=-*,clang-analyzer-*,performance-*,bugprone-* -fix ${_file} -- -std=c++17 -DDEBUG ${MY_PROJECT_HEADER_PATHS}
      )
    list(APPEND tidyfied ${_file}.noop)
  endif()
endforeach()
add_custom_target(tidy DEPENDS ${tidyfied})
###

if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
  include_directories(/usr/local/include)
  link_directories(/usr/local/lib)
endif()

ExternalProject_Add(prometheus_a
  GIT_REPOSITORY    https://github.com/shards-lang/prometheus-cpp.git
  GIT_TAG           8400b647f46a017222f9b9d5ca300ed20090d71f
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/prometheus_a
  CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DENABLE_PUSH=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/prometheus_a
  BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/prometheus_a/lib/libprometheus-cpp-core.a ${CMAKE_CURRENT_BINARY_DIR}/prometheus_a/lib/libprometheus-cpp-pull.a
  )

include_directories(${CMAKE_CURRENT_BINARY_DIR}/prometheus_a/include)

add_library(libprometheus STATIC IMPORTED GLOBAL)
add_dependencies(libprometheus prometheus_a)
set_target_properties(libprometheus PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/prometheus_a/lib/libprometheus-cpp-core.a)
add_library(libprometheus-pull STATIC IMPORTED GLOBAL)
add_dependencies(libprometheus-pull prometheus_a)
set_target_properties(libprometheus-pull PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/prometheus_a/lib/libprometheus-cpp-pull.a)

include_directories(
  ${SHARDS_DIR}/include
  ${SHARDS_DIR}/deps/stb
  ${SHARDS_DIR}/deps/cpp-taskflow
  ${SHARDS_DIR}/deps/nameof/include
  ${SHARDS_DIR}/deps/magic_enum/include
  ${SHARDS_DIR}/deps/spdlog/include
  ${SHARDS_DIR}/deps/entt/src
  )

add_library(
  cbprometheus SHARED
  ${CMAKE_CURRENT_LIST_DIR}/prometheus.cpp
  )

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  # 64 bits
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  # 32 bits
  add_compile_definitions(I386_BUILD)
endif()
###

if(WIN32)
  target_link_libraries(cbprometheus -static libprometheus-pull libprometheus -lz)
else()
  target_link_libraries(cbprometheus libprometheus-pull libprometheus -lz)
endif()

set_target_properties(cbprometheus PROPERTIES PREFIX "")
set_target_properties(cbprometheus PROPERTIES OUTPUT_NAME "prometheus")
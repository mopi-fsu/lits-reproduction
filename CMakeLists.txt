cmake_minimum_required(VERSION 3.16)
project(lits_reproduction VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITS_BUILD_BENCHMARKS "Build the benchmark entry point" ON)

add_library(lits_options INTERFACE)
target_include_directories(lits_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(lits_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

# Opi: HPT, local prediction, GPKL, and PMSS.
add_library(lits_model src/model/hpt.cpp src/model/gpkl.cpp src/model/pmss.cpp)
target_link_libraries(lits_model PUBLIC lits_options)

# Oyshi: compact leaves and ordered scans.
add_library(lits_leaf src/leaf/compact_leaf.cpp)
target_link_libraries(lits_leaf PUBLIC lits_options)

# Bach: tree structure, operations, and integration.
add_library(lits_core src/core/index.cpp)
target_link_libraries(lits_core PUBLIC lits_model lits_leaf)
add_library(lits::core ALIAS lits_core)

add_executable(lits_example examples/basic.cpp)
target_link_libraries(lits_example PRIVATE lits::core)

if(LITS_BUILD_BENCHMARKS)
    add_executable(lits_bench benchmarks/main.cpp)
    target_link_libraries(lits_bench PRIVATE lits::core)
endif()

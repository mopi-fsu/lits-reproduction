cmake_minimum_required(VERSION 3.16)
project(lits_reproduction VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITS_BUILD_BENCHMARKS "Build the benchmark entry point" ON)
option(LITS_BUILD_TESTS "Build the correctness tests" ON)

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
add_library(lits_core src/core/index.cpp src/core/node.cpp src/core/validation.cpp
    src/core/routing.cpp)
target_link_libraries(lits_core PUBLIC lits_model lits_leaf)
add_library(lits::core ALIAS lits_core)

add_executable(lits_example examples/basic.cpp)
target_link_libraries(lits_example PRIVATE lits::core)

if(LITS_BUILD_BENCHMARKS)
    add_executable(lits_bench benchmarks/main.cpp)
    target_link_libraries(lits_bench PRIVATE lits::core)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(lits_core_tests tests/core/test_index.cpp)
    target_link_libraries(lits_core_tests PRIVATE lits::core)
    add_test(NAME core COMMAND lits_core_tests)

    add_executable(lits_index_diff_tests tests/core/test_index_diff.cpp)
    target_link_libraries(lits_index_diff_tests PRIVATE lits::core)
    add_test(NAME index_diff COMMAND lits_index_diff_tests)
    # Different random sequences. Seeds 2, 5, 13, 25 and 44 exposed the rounding bug in
    # the model's CDF ordering before routing was made exact; keep them as regressions.
    foreach(seed 1 2 5 13 25 44)
        add_test(NAME index_diff_seed_${seed} COMMAND lits_index_diff_tests ${seed})
    endforeach()

    add_executable(lits_routing_tests tests/core/test_routing.cpp)
    target_link_libraries(lits_routing_tests PRIVATE lits::core)
    add_test(NAME routing COMMAND lits_routing_tests)

    add_executable(gpkl_tests tests/model/gpkl_test.cpp)
    target_link_libraries(gpkl_tests PRIVATE lits_model)
    add_test(NAME gpkl COMMAND gpkl_tests)

    add_executable(hpt_training_tests tests/model/hpt_training_test.cpp)
    target_link_libraries(hpt_training_tests PRIVATE lits_model)
    add_test(NAME hpt_training COMMAND hpt_training_tests)

    add_executable(hpt_cdf_tests tests/model/hpt_cdf_test.cpp)
    target_link_libraries(hpt_cdf_tests PRIVATE lits_model)
    add_test(NAME hpt_cdf COMMAND hpt_cdf_tests)

    add_executable(local_model_tests tests/model/local_model_test.cpp)
    target_link_libraries(local_model_tests PRIVATE lits_model)
    add_test(NAME local_model COMMAND local_model_tests)

    add_executable(compact_leaf_tests tests/leaf/compact_leaf_test.cpp)
    target_link_libraries(compact_leaf_tests PRIVATE lits_leaf)
    add_test(NAME compact_leaf COMMAND compact_leaf_tests)
endif()

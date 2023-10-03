if(NOT FAST_DOWNWARD_MAIN_CMAKELISTS_READ)
    message(
        FATAL_ERROR
        "Run cmake on the CMakeLists.txt in the 'src' directory. "
        "Please delete CMakeCache.txt from the current directory "
        "and restart cmake.")
endif()

cmake_minimum_required(VERSION 3.16)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ext)

# Path containing custom CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../search/cmake)
include(common_cxx_flags)
include(macros)
include(options)

report_bitwidth()
set_up_build_types("Debug;Release")

project(preprocess-h2 LANGUAGES CXX)

set(PREPROCESS_SOURCES
    planner.cc
    axiom.cc
    causal_graph.cc
    h2_mutexes.cc
    helper_functions.cc
    max_dag.cc
    mutex_group.cc
    operator.cc
    scc.cc
    state.cc
    variable.cc
)

add_executable(preprocess-h2 ${PREPROCESS_SOURCES})
target_link_libraries(preprocess-h2 INTERFACE common_cxx_flags)

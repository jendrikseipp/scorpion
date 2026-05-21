# Fast Downward translator, C++ port.
#
# This is a standalone executable that reads PDDL (domain + problem)
# and writes a SAS+ task file (output.sas), mirroring the behavior of
# src/translate (the Python translator).

cmake_minimum_required(VERSION 3.16)

project(downward_translate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wnon-virtual-dtor
                        -Wfloat-conversion -Wmissing-declarations
                        -Wzero-as-null-pointer-constant)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    add_compile_options(-g)
endif()

set(TRANSLATE_SOURCES
    translator_main.cc
    translate_options.cc

    utils/graph.cc
    utils/logging.cc
    utils/strings.cc
    utils/system.cc
    utils/timer.cc

    pddl/types.cc
    pddl/predicate.cc
    pddl/condition.cc
    pddl/f_expression.cc
    pddl/effect.cc
    pddl/action.cc
    pddl/axiom.cc
    pddl/task.cc

    parser/lisp_parser.cc
    parser/parser.cc

    normalize/normalize.cc

    grounding/program.cc
    grounding/build.cc
    grounding/split.cc
    grounding/model.cc

    instantiate/instantiate.cc

    invariants/constraints.cc
    invariants/invariants.cc
    invariants/invariant_finder.cc
)

add_executable(translate ${TRANSLATE_SOURCES})

target_include_directories(translate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

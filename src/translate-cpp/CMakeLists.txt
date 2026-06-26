# Fast Downward translator, C++ port.
#
# This is a standalone executable that reads PDDL (domain + problem)
# and writes a SAS+ task file (output.sas), mirroring the behavior of
# src/translate (the Python translator).

cmake_minimum_required(VERSION 3.16)

# If it's available, use ccache to cache compilation results, mirroring the
# top-level src/CMakeLists.txt. The two ccache options allow sharing
# compilation results between different build directories.
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND AND NOT WIN32)  # Windows GitHub Actions find "ccache" --> ignore it.
    message("Using ccache")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
        "CCACHE_BASEDIR=${CMAKE_CURRENT_SOURCE_DIR} CCACHE_NOHASHDIR=true ccache")
endif()

project(downward_translate LANGUAGES CXX)

# The C++ standard, optimization, warning, and build-type handling below
# mirror the search component (src/search/cmake/common_cxx_flags.cmake and
# macros.cmake). We keep our own copy so this directory stays self-contained
# and can be built on its own.

# Default to a release build (mirrors set_up_build_types).
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    message(STATUS "Defaulting to release build.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug;Release")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-O3 -g
        -Wall -Wextra -Wpedantic -Wnon-virtual-dtor -Wfloat-conversion
        -Wmissing-declarations -Wzero-as-null-pointer-constant)
    add_compile_options("$<$<CONFIG:Release>:-DNDEBUG;-fomit-frame-pointer>")
    add_link_options(-g)
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

    fact_groups/fact_groups.cc

    utils/sccs.cc
    axioms/axiom_rules.cc

    sas/sas_task.cc

    simplify/simplify.cc
    simplify/variable_order.cc

    pipeline/translate.cc
)

add_executable(translate ${TRANSLATE_SOURCES})

target_include_directories(translate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

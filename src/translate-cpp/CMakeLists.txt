# Fast Downward translator, C++ port.

cmake_minimum_required(VERSION 3.16)

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
    # clang >= 15 warns about unqualified std::move/std::forward calls, but we
    # use `using namespace std;` throughout. Disable it (mirrors src/search).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang"
       AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
        add_compile_options(-Wno-unqualified-std-cast-call)
    endif()
elseif(MSVC)
    # Enable exceptions and use warning level 4, mirroring src/preprocess_h2.
    # Disable warnings that currently trigger in the code (same set as the
    # other components); the Windows CI builds with /WX (warnings as errors).
    add_compile_options(/EHsc /W4
        /wd4100  # unreferenced formal parameter
        /wd4127  # conditional expression is constant
        /wd4244  # conversion with possible loss of data
        /wd4267  # conversion from size_t to int with possible loss of data
        /wd4456  # declaration hides previous local declaration
        /wd4458  # declaration hides class member
        /wd4459  # declaration hides global declaration
        /wd4702) # unreachable code
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

# The target is named translate-cpp (not translate) so its binary does not
# collide with the Python translator package that the build drops at
# builds/<config>/bin/translate/.
add_executable(translate-cpp ${TRANSLATE_SOURCES})

target_include_directories(translate-cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

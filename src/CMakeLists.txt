# Usage:
#         cmake -S src -B builds/release
#         cmake --build builds/release
# The first call caches settings in the build directory and reads
# them from the cache on subsequent builds. If you want to change the
# settings of some options, do _not_ change the CMakeLists.txt files.
# Instead, create a new build directory, pass -DMY_OPTION=my_value to
# cmake. Alternatively, you can use a cmake GUI like ccmake to edit
# the cache.
#
# Two build targets are defined:
#
# * release (default)
#      -O3 optimisation, debugging symbols, assertions inactive
# * debug
#      -O3 optimisation, full debugging information, assertions active
#
# In all build targets, we overwrite the default configuration to
# include "-g", allow cross compilation and switch to pedantic error
# reporting.
#
# You can change the build target, by adding the parameter
#   -DCMAKE_BUILD_TYPE=type
# to the cmake call.

cmake_minimum_required(VERSION 3.16)

# If it's available, use ccache to cache compilation results. The two ccache options
# allow sharing compilation results between different build directories.
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND AND NOT WIN32)  # Windows Github Actions find "ccache" --> ignore it.
    message("Using ccache")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
        "CCACHE_BASEDIR=${CMAKE_CURRENT_SOURCE_DIR} CCACHE_NOHASHDIR=true ccache")
endif(CCACHE_FOUND AND NOT WIN32)

project(fast-downward
    DESCRIPTION "Fast Downward is a domain-independent classical planning system."
    HOMEPAGE_URL https://www.fast-downward.org/)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

# Add planner components.

# Copy the translator into the output directory.
add_custom_target(translate ALL)
add_custom_command(TARGET translate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/translate
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/translate
    COMMENT "Copying translator module into output directory")

# Add search component as a subproject.
add_subdirectory(preprocess_h2)
add_subdirectory(search)

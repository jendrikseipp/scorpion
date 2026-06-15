#ifndef UTILS_SYSTEM_H
#define UTILS_SYSTEM_H

#define LINUX 0
#define OSX 1
#define WINDOWS 2

#if defined(_WIN32)
#define OPERATING_SYSTEM WINDOWS
#include "system_windows.h"
#elif defined(__APPLE__)
#define OPERATING_SYSTEM OSX
#include "system_unix.h"
#else
#define OPERATING_SYSTEM LINUX
#include "system_unix.h"
#endif

#include "language.h"

#include <iostream>
#include <stdlib.h>

/*
  macOS builds use libc++ (via Homebrew's LLVM and -fexperimental-library). Its
  parallel STL implements the <algorithm> execution-policy overloads but not the
  <numeric> ones, so transform_reduce(std::execution::unseq, ...) does not
  compile. We provide a serial fallback overload with the same signature here so
  that existing call sites work unchanged. Since unseq is only a vectorization
  hint, the serial fallback is semantically identical. The overload lives in the
  global namespace so that unqualified calls find it, and is only defined for
  libc++; on libstdc++ (Linux) the block is skipped and the standard parallel
  overload is used.
*/
#ifdef _LIBCPP_VERSION
#include <execution>
#include <numeric>
#include <utility>

template<typename InputIt, typename T, typename BinaryOp, typename UnaryOp>
T transform_reduce(
    const std::execution::unsequenced_policy &, InputIt first, InputIt last,
    T init, BinaryOp binary_op, UnaryOp unary_op) {
    return std::transform_reduce(
        first, last, std::move(init), binary_op, unary_op);
}
#endif

#define ABORT(msg) \
    ((std::cerr << "Critical error in file " << __FILE__ << ", line " \
                << __LINE__ << ": " << std::endl \
                << (msg) << std::endl), \
     (abort()), (void)0)

namespace utils {
enum class ExitCode {
    /*
      For a full list of exit codes, please see driver/returncodes.py. Here,
      we only list codes that are used by the search component of the planner.
    */
    // 0-9: exit codes denoting a plan was found
    SUCCESS = 0,

    // 10-19: exit codes denoting no plan was found (without any error)
    SEARCH_UNSOLVABLE = 11, // Task is provably unsolvable with given bound.
    SEARCH_UNSOLVED_INCOMPLETE = 12, // Search ended without finding a solution.

    // 20-29: "expected" failures
    SEARCH_OUT_OF_MEMORY = 22,
    SEARCH_OUT_OF_TIME = 23,

    // 30-39: unrecoverable errors
    SEARCH_CRITICAL_ERROR = 32,
    SEARCH_INPUT_ERROR = 33,
    SEARCH_UNSUPPORTED = 34
};

class ExitException : public std::exception {
    ExitCode exitcode;
public:
    explicit ExitException(ExitCode exitcode) : exitcode(exitcode) {
    }

    ExitCode get_exitcode() const {
        return exitcode;
    }
};

NO_RETURN extern void exit_with(ExitCode returncode);
NO_RETURN extern void exit_with_reentrant(ExitCode returncode);

int get_peak_memory_in_kb();
const char *get_exit_code_message_reentrant(ExitCode exitcode);
bool is_exit_code_error_reentrant(ExitCode exitcode);
void register_event_handlers();
void report_exit_code(ExitCode exitcode);
void report_exit_code_reentrant(ExitCode exitcode);
int get_process_id();
}

#endif

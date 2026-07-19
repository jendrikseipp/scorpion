#ifndef UTILS_EXECUTION_H
#define UTILS_EXECUTION_H

#include <algorithm>
#include <utility>
#include <version>

/*
  libc++ only ships a partial, experimental parallel STL: Apple Clang
  disables it entirely, and Homebrew LLVM hides it behind
  -fexperimental-library and misses the <numeric> overloads such as
  transform_reduce(). Therefore, we call the serial algorithms on libc++ and
  on all other standard libraries without parallel algorithm support. Since
  std::execution::unseq is only a vectorization hint, the serial algorithms
  are semantically identical.
*/
#if defined(__cpp_lib_parallel_algorithm) && !defined(_LIBCPP_VERSION)
#include <execution>
#define UNSEQ_POLICY std::execution::unseq,
#else
#define UNSEQ_POLICY
#endif

namespace utils {
/*
  Pass utils::unseq instead of std::execution::unseq to the wrappers below,
  which forward to the standard algorithms and drop the execution policy on
  systems without parallel STL support.
*/
struct UnsequencedPolicy {};
inline constexpr UnsequencedPolicy unseq{};

/*
  We only wrap algorithms that measurably benefit from the hint (measured on
  x86-64 with -O3 and no -march, using libstdc++):
    any_of: serial std::any_of cannot vectorize (short-circuit exit); with
            unseq it does, on both GCC and Clang.
    find:   unseq vectorizes on Clang; GCC leaves it scalar (harmless).
  std::count already auto-vectorizes without the hint, and sort, transform and
  transform_reduce showed no reliable gain, so those call the serial algorithms
  directly rather than going through here.
*/
template<typename... Args>
auto any_of(UnsequencedPolicy, Args &&...args) {
    return std::any_of(UNSEQ_POLICY std::forward<Args>(args)...);
}

template<typename... Args>
auto find(UnsequencedPolicy, Args &&...args) {
    return std::find(UNSEQ_POLICY std::forward<Args>(args)...);
}
}

#undef UNSEQ_POLICY

#endif

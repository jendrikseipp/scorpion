#ifndef UTILS_MATH_H
#define UTILS_MATH_H

namespace utils {
/* Test if the product of two numbers is bounded by a third number.
   Safe against overflow. The caller must guarantee
   0 <= factor1, factor2 <= limit; failing this is an error. */
extern bool is_product_within_limit(
    long long factor1, long long factor2, long long limit);
}

#endif

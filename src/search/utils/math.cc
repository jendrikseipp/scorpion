#include "math.h"

#include <cassert>

namespace utils {
bool is_product_within_limit(
    long long factor1, long long factor2, long long limit) {
    assert(factor1 >= 0);
    assert(factor2 >= 0);
    assert(limit >= 0);
    return factor2 == 0 || factor1 <= limit / factor2;
}
}

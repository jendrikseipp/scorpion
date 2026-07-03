#ifndef UTILS_HASH_H
#define UTILS_HASH_H

#include <cstddef>

namespace translate::utils {
// boost::hash_combine-style mixing: fold `value` into `seed`. The magic
// constant is the 64-bit golden ratio; the shifts spread entropy across all
// bits. Hash values never affect the translator's output (every consumer sorts
// before emitting), so this only needs to be a decent, stable mixer.
inline void hash_combine(std::size_t &seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}

#endif

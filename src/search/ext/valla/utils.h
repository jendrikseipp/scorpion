//
// Created by workbox on 6/17/25.
//

#ifndef UTILS_H
#define UTILS_H
#include <utility>

template<typename T>
inline bool order_pair(std::pair<T, T> &p) {
    bool swapped = p.first > p.second;
    T mask = -static_cast<T>(swapped); // All bits set if swapped, else 0
    T tmp = (p.first ^ p.second) & mask;
    p.first ^= tmp;
    p.second ^= tmp;
    return swapped;
}
#endif //UTILS_H

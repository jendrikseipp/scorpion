//
// Created by workbox on 6/15/25.
//

#ifndef STORAGE_CALC_H
#define STORAGE_CALC_H

constexpr size_t entries_for_mb(size_t limit_mb, size_t type_size) {
    return (limit_mb * 1024 * 1024) / type_size;
}
#endif //STORAGE_CALC_H

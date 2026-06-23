#ifndef PER_STATE_BITSET_H
#define PER_STATE_BITSET_H

#include "per_state_array.h"

#include <vector>

class BitsetMath {
public:
    using Block = unsigned char;
    static_assert(
        !std::numeric_limits<Block>::is_signed, "Block type must be unsigned");

    static const Block zeros = Block(0);
    // MSVC's bitwise negation always returns a signed type.
    static const Block ones = Block(~Block(0));
    static const int bits_per_block = std::numeric_limits<Block>::digits;

    static int compute_num_blocks(std::size_t num_bits);
    static std::size_t block_index(std::size_t pos);
    static std::size_t bit_index(std::size_t pos);
    static Block bit_mask(std::size_t pos);
};

class ConstBitsetView {
    ConstArrayView<BitsetMath::Block> data;
    int num_bits;
public:
    ConstBitsetView(ConstArrayView<BitsetMath::Block> data, int num_bits);

    ConstBitsetView(const ConstBitsetView &other) = default;
    ConstBitsetView &operator=(const ConstBitsetView &other) = default;

    bool test() const;
    bool test(int index) const;
    int count() const;
    bool intersects(const ConstBitsetView &other) const;
    bool is_subset_of(const ConstBitsetView &other) const;
    int size() const;

    friend std::ostream &operator<<(
        std::ostream &os, const ConstBitsetView &view) {
        for (int index = 0; index < view.num_bits; ++index) {
            os << view.test(index);
        }
        return os;
    }
};

class BitsetView {
    ArrayView<BitsetMath::Block> data;
    int num_bits;

    void zero_unused_bits();
public:
    BitsetView(ArrayView<BitsetMath::Block> data, int num_bits)
        : data(data), num_bits(num_bits) {
    }

    BitsetView(const BitsetView &other) = default;
    BitsetView &operator=(const BitsetView &other) = default;

    operator ConstBitsetView() const {
        return ConstBitsetView(data, num_bits);
    }

    void set(int index);
    void set();
    void reset(int index);
    void reset();
    bool test(int index) const;
    void intersect(const BitsetView &other);
    bool intersects(const BitsetView &other) const;
    int size() const;

    friend std::ostream &operator<<(std::ostream &os, const BitsetView &view) {
        for (int index = 0; index < view.num_bits; ++index) {
            os << view.test(index);
        }
        return os;
    }
};

class PerStateBitset {
    int num_bits_per_entry;
    PerStateArray<BitsetMath::Block> data;
public:
    explicit PerStateBitset(const std::vector<bool> &default_bits);

    PerStateBitset(const PerStateBitset &) = delete;
    PerStateBitset &operator=(const PerStateBitset &) = delete;

    BitsetView operator[](const State &state);
    ConstBitsetView operator[](const State &state) const;
};

#endif

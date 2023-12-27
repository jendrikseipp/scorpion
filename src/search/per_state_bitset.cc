#include "per_state_bitset.h"

#include <bit>

using namespace std;


int BitsetMath::compute_num_blocks(size_t num_bits) {
    return (num_bits + bits_per_block - 1) / bits_per_block;
}

size_t BitsetMath::block_index(size_t pos) {
    return pos / bits_per_block;
}

size_t BitsetMath::bit_index(size_t pos) {
    return pos % bits_per_block;
}

BitsetMath::Block BitsetMath::bit_mask(size_t pos) {
    return Block(1) << bit_index(pos);
}


void BitsetView::zero_unused_bits() {
    int bits_in_last_block = BitsetMath::bit_index(num_bits);
    if (bits_in_last_block != 0) {
        assert(data.size() != 0);
        data[data.size() - 1] &= ~(BitsetMath::ones << bits_in_last_block);
    }
}

void BitsetView::set(int index) {
    assert(index >= 0 && index < num_bits);
    int block_index = BitsetMath::block_index(index);
    data[block_index] |= BitsetMath::bit_mask(index);
}

void BitsetView::set() {
    for (int i = 0; i < data.size(); ++i) {
        data[i] = BitsetMath::ones;
    }
    zero_unused_bits();
}

void BitsetView::reset(int index) {
    assert(index >= 0 && index < num_bits);
    int block_index = BitsetMath::block_index(index);
    data[block_index] &= ~BitsetMath::bit_mask(index);
}

void BitsetView::reset() {
    for (int i = 0; i < data.size(); ++i) {
        data[i] = BitsetMath::zeros;
    }
}

bool BitsetView::test(int index) const {
    assert(index >= 0 && index < num_bits);
    int block_index = BitsetMath::block_index(index);
    return (data[block_index] & BitsetMath::bit_mask(index)) != 0;
}

void BitsetView::intersect(const BitsetView &other) {
    assert(num_bits == other.num_bits);
    for (int i = 0; i < data.size(); ++i) {
        data[i] &= other.data[i];
    }
}

bool BitsetView::intersects(const BitsetView &other) const {
    assert(num_bits == other.num_bits);
    for (int i = 0; i < data.size(); ++i) {
        if (data[i] & other.data[i]) {
            return true;
        }
    }
    return false;
}

int BitsetView::size() const {
    return num_bits;
}


ConstBitsetView::ConstBitsetView(ConstArrayView<BitsetMath::Block> data, int num_bits) :
    data(data), num_bits(num_bits) {}

bool ConstBitsetView::test() const {
    assert(data.size() > 0);
    // Check that first n-1 blocks are fully set.
    for (int i = 0; i < data.size() - 1; ++i) {
        if (data[i] != BitsetMath::ones) {
            return false;
        }
    }

    // Check last block.
    int bits_in_last_block = BitsetMath::bit_index(num_bits);
    if (bits_in_last_block == 0) {
        bits_in_last_block = BitsetMath::bits_per_block;
    }
    int empty_positions_in_last_block = BitsetMath::bits_per_block - bits_in_last_block;
    return data[data.size() - 1] == (BitsetMath::ones >> empty_positions_in_last_block);
}


bool ConstBitsetView::test(int index) const {
    assert(index >= 0 && index < num_bits);
    int block_index = BitsetMath::block_index(index);
    return (data[block_index] & BitsetMath::bit_mask(index)) != 0;
}

int ConstBitsetView::count() const {
    int result = 0;
    for (int i = 0; i < data.size(); ++i) {
        result += popcount(data[i]);
    }
#ifndef NDEBUG
    int slow_result = 0;
    for (int index = 0; index < num_bits; ++index) {
        slow_result += test(index);
    }
    assert(result == slow_result);
#endif
    return result;
}

bool ConstBitsetView::intersects(const ConstBitsetView &other) const {
    assert(num_bits == other.num_bits);
    for (int i = 0; i < data.size(); ++i) {
        if (data[i] & other.data[i]) {
            return true;
        }
    }
    return false;
}

bool ConstBitsetView::is_subset_of(const ConstBitsetView &other) const {
    assert(num_bits == other.num_bits);
    for (int i = 0; i < data.size(); ++i) {
        if (data[i] & ~other.data[i]) {
            return false;
        }
    }
    return true;
}

int ConstBitsetView::size() const {
    return num_bits;
}


static vector<BitsetMath::Block> pack_bit_vector(const vector<bool> &bits) {
    int num_bits = bits.size();
    int num_blocks = BitsetMath::compute_num_blocks(num_bits);
    vector<BitsetMath::Block> packed_bits(num_blocks, 0);
    BitsetView bitset_view(ArrayView<BitsetMath::Block>(packed_bits.data(), num_blocks), num_bits);
    for (int i = 0; i < num_bits; ++i) {
        if (bits[i]) {
            bitset_view.set(i);
        }
    }
    return packed_bits;
}


PerStateBitset::PerStateBitset(const vector<bool> &default_bits)
    : num_bits_per_entry(default_bits.size()),
      data(pack_bit_vector(default_bits)) {
}

BitsetView PerStateBitset::operator[](const State &state) {
    return BitsetView(data[state], num_bits_per_entry);
}

ConstBitsetView PerStateBitset::operator[](const State &state) const {
    return ConstBitsetView(data[state], num_bits_per_entry);
}

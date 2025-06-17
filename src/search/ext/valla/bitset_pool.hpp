/*
 * Copyright (C) 2025 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef VALLA_INCLUDE_BITSET_POOL_HPP_
#define VALLA_INCLUDE_BITSET_POOL_HPP_

#include "valla/declarations.hpp"

#include <bitset>
#include <limits>
#include <vector>
#include <cassert>
#include <algorithm>

namespace valla
{

// === BitBlock bit storage abstraction ===
// Edit the next line for global tuning:
using BitBlock = uint64_t;
constexpr size_t BitBlockBits = sizeof(BitBlock) * 8;

class BitsetPool;
class Bitset;

class Bitset
{
private:
    BitBlock* m_blocks;
    uint32_t m_num_bits;
    Index m_index;

public:
    Bitset();
    Bitset(BitBlock* blocks, uint32_t num_bits, Index index);

    bool get(size_t bit) const;
    void set(size_t bit);

    BitBlock* get_blocks() const;
    uint32_t get_num_bits() const;
    uint32_t get_num_blocks() const;
    Index get_index() const;
};

static_assert(sizeof(Bitset) == 16);
static_assert(sizeof(Bitset*) == 8);

struct BitsetHash
{
    size_t operator()(const Bitset& el) const;
};

struct BitsetEqualTo
{
    bool operator()(const Bitset& lhs, const Bitset& rhs) const;
};

struct DerefBitsetHash
{
    size_t operator()(const Bitset* el) const;
};

struct DerefBitsetEqualTo
{
    bool operator()(const Bitset* lhs, const Bitset* rhs) const;
};

class BitsetPool
{
private:
    std::vector<std::vector<BitBlock>> m_segments;
    size_t m_segment;
    size_t m_offset;
    size_t m_size;

    size_t m_last_allocated_num_blocks;

    static constexpr const size_t INITIAL_SEGMENT_SIZE = 1024;

    void resize_to_fit(size_t num_blocks);

public:
    BitsetPool();

    Bitset allocate(uint32_t num_bits);

    void pop_allocation();

    std::vector<BitBlock>& get_segment(size_t segment);

    const std::vector<BitBlock>& get_segment(size_t segment) const;

    size_t size() const;

    size_t estimate_memory_usage() const;
};

class BitsetRepository
{
private:
    std::vector<std::vector<Bitset>> m_segments;
    size_t m_offset;
    size_t m_size;

    phmap::flat_hash_set<const Bitset*, DerefBitsetHash, DerefBitsetEqualTo> m_uniqueness;

    const Bitset* m_empty_bitset;

    static size_t get_index(size_t pos) { return std::countr_zero(std::bit_floor(pos + 1)); }

    static size_t get_offset(size_t pos) { return pos - (std::bit_floor(pos + 1) - 1); }

    void resize_to_fit();

public:
    explicit BitsetRepository(BitsetPool& pool);

    const Bitset& get_empty_bitset() const;

    const Bitset& operator[](size_t pos) const;

    auto insert(Bitset bitset);

    size_t size() const;

    size_t estimate_memory_usage() const;
};

/**
 * Bitset
 */

inline Bitset::Bitset() : m_blocks(nullptr), m_num_bits(0), m_index(0) {}

inline Bitset::Bitset(BitBlock* blocks, uint32_t num_bits, Index index)
    : m_blocks(blocks), m_num_bits(num_bits), m_index(index) {}

inline bool Bitset::get(size_t bit) const
{
    assert(bit < get_num_bits());

    size_t block_index = bit / BitBlockBits;
    size_t bit_index   = bit % BitBlockBits;

    return (m_blocks[block_index] & (BitBlock(1) << bit_index)) != 0;
}

inline void Bitset::set(size_t bit)
{
    assert(bit < get_num_bits());

    size_t block_index = bit / BitBlockBits;
    size_t bit_index   = bit % BitBlockBits;

    m_blocks[block_index] |= (BitBlock(1) << bit_index);
}

inline BitBlock* Bitset::get_blocks() const { return m_blocks; }

inline uint32_t Bitset::get_num_bits() const { return m_num_bits; }

inline uint32_t Bitset::get_num_blocks() const { return (m_num_bits + BitBlockBits - 1) / BitBlockBits; }

inline Index Bitset::get_index() const { return m_index; }

/**
 * BitsetHash
 */

inline size_t BitsetHash::operator()(const Bitset& el) const
{
    size_t seed = el.get_num_bits();
    for (size_t i = 0; i < el.get_num_blocks(); ++i)
    {
        valla::hash_combine(seed, el.get_blocks()[i]);
    }
    return seed;
}

/**
 * BitsetEqualTo
 */

inline bool BitsetEqualTo::operator()(const Bitset& lhs, const Bitset& rhs) const
{
    if (lhs.get_num_bits() != rhs.get_num_bits())
        return false;
    return std::equal(lhs.get_blocks(), lhs.get_blocks() + lhs.get_num_blocks(), rhs.get_blocks());
}

/**
 * BitsetHash
 */

inline size_t DerefBitsetHash::operator()(const Bitset* el) const { return BitsetHash{}(*el); }

/**
 * BitsetEqualTo
 */

inline bool DerefBitsetEqualTo::operator()(const Bitset* lhs, const Bitset* rhs) const { return BitsetEqualTo{}(*lhs, *rhs); }

/**
 * BitsetPool
 */

inline void BitsetPool::resize_to_fit(size_t num_blocks)
{
    const auto remaining_blocks = m_segments.back().size() - m_offset;

    if (remaining_blocks < num_blocks)
    {
        const auto new_segment_size = std::max(m_segments.back().size() * 2, num_blocks);

        m_segments.push_back(std::vector<BitBlock>(new_segment_size, BitBlock(0)));
        ++m_segment;
        m_offset = 0;
    }
}

inline BitsetPool::BitsetPool() :
    m_segments(1, std::vector<BitBlock>(INITIAL_SEGMENT_SIZE, BitBlock(0))),
    m_segment(0),
    m_offset(0),
    m_size(0),
    m_last_allocated_num_blocks(0)
{
}

inline Bitset BitsetPool::allocate(uint32_t num_bits)
{
    const auto num_blocks = num_bits / BitBlockBits + (num_bits % BitBlockBits != 0 ? 1 : 0);
    resize_to_fit(num_blocks);

    auto view = Bitset(m_segments.back().data() + m_offset, num_bits, m_size++);
    m_offset += num_blocks;
    m_last_allocated_num_blocks = num_blocks;
    return view;
}

inline void BitsetPool::pop_allocation()
{
    assert(m_offset >= m_last_allocated_num_blocks);

    auto& segment = m_segments.back();
    std::fill(segment.begin() + m_offset - m_last_allocated_num_blocks, segment.begin() + m_offset, BitBlock(0));
    m_offset -= m_last_allocated_num_blocks;
    --m_size;
    m_last_allocated_num_blocks = 0;
}

inline std::vector<BitBlock>& BitsetPool::get_segment(size_t segment)
{
    assert(segment < m_segments.size());
    return m_segments[segment];
}

inline const std::vector<BitBlock>& BitsetPool::get_segment(size_t segment) const
{
    assert(segment < m_segments.size());
    return m_segments[segment];
}

inline size_t BitsetPool::size() const { return m_size; }

inline size_t BitsetPool::estimate_memory_usage() const
{
    size_t total = 0;
    for (const auto& segment : m_segments) {
        total += segment.capacity() * sizeof(BitBlock);
    }
    return total;
}

/**
 * BitsetRepository
 */

inline void BitsetRepository::resize_to_fit()
{
    const auto remaining_entries = m_segments.back().size() - m_offset;

    if (remaining_entries == 0)
    {
        const auto new_segment_size = m_segments.back().size() * 2;
        m_segments.push_back(std::vector<Bitset>(new_segment_size));
        m_offset = 0;
    }
}

inline const Bitset& BitsetRepository::get_empty_bitset() const { return *m_empty_bitset; }

inline const Bitset& BitsetRepository::operator[](size_t pos) const
{
    assert(pos < size());
    const auto index = get_index(pos);
    const auto offset = get_offset(pos);
    return m_segments[index][offset];
}

inline auto BitsetRepository::insert(Bitset bitset)
{
    resize_to_fit();

    auto& element = m_segments.back()[m_offset] = bitset;

    auto result = m_uniqueness.insert(&element);

    if (result.second)
    {
        ++m_offset;
        ++m_size;
    }

    return result;
}

inline BitsetRepository::BitsetRepository(BitsetPool& pool)
    : m_segments(), m_offset(0), m_size(0), m_uniqueness(), m_empty_bitset(nullptr)
{
    m_segments.resize(1);
    m_segments.back().resize(1);
    m_empty_bitset = *insert(pool.allocate(0)).first;
}

inline size_t BitsetRepository::size() const { return m_uniqueness.size(); }


inline size_t BitsetRepository::estimate_memory_usage() const
{
    size_t total = 0;
    for (const auto& segment : m_segments) {
        total += segment.capacity() * sizeof(Bitset);
    }
    total += m_uniqueness.size() * sizeof(Bitset*);
    return total;
}

}
#endif
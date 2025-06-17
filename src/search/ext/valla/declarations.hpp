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

#ifndef VALLA_INCLUDE_DECLARATIONS_HPP_
#define VALLA_INCLUDE_DECLARATIONS_HPP_

#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fixed_hash_set.hpp"
#include "dynamic_bitset.h"

namespace valla
{
// Observe: there is no sentinel value requires in constrast to several other tree compression data structures.

using Index = uint32_t;  ///< Enough space for 4,294,967,295 indices
using Slot = uint64_t;   ///< A slot is made up of two indices.

static constexpr Slot EMPTY_ROOT_SLOT = Slot(0);  ///< represents the empty state.

/// @brief Pack two uint32_t into a uint64_t.
inline Slot make_slot(Index lhs, Index rhs) { return Slot(lhs) << 32 | rhs; }

/// @brief Unpack two uint32_t from a uint64_t.
inline std::pair<Index, Index> read_slot(Slot slot) { return { Index(slot >> 32), slot & (Index(-1)) }; }

inline Index read_pos(Slot slot, size_t pos)
{
    assert(pos < 2);
    return Index((slot >> ((1 - pos) * 32)));
}

using State = std::vector<Index>;
using RootIndices = std::vector<Index>;


    using State = std::vector<Index>;
    using RootIndices = std::vector<Index>;

    /**
     * Printing
     */

    inline std::ostream& operator<<(std::ostream& out, const State& state)
    {
        out << "[";
        for (const auto x : state)
        {
            out << x << ", ";
        }
        out << "]";

        return out;
    }

    /**
     * Hashing
     */

    template<typename T>
    inline void hash_combine(size_t& seed, const T& value)
    {
        seed ^= std::hash<std::decay_t<T>> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    inline uint64_t cantor_pair(uint64_t a, uint64_t b) { return (((a + b) * (a + b + 1)) >> 1) + b; }

    struct SlotHash
    {
        size_t operator()(Slot el) const { return cantor_pair(read_pos(el, 0), read_pos(el, 1)); }
    };



    template<typename LHS = uint32_t, typename RHS = uint32_t, typename ALIGNMENT = uint64_t>
    struct alignas(ALIGNMENT) SlotStruct {
        LHS lhs;
        RHS rhs;

        bool operator==(const SlotStruct& other) const {
            return *reinterpret_cast<const ALIGNMENT*>(this) == *reinterpret_cast<const ALIGNMENT*>(&other);
        }

        static constexpr SlotStruct<LHS, RHS, ALIGNMENT> EmptySentinel{
            std::numeric_limits<LHS>::max(), std::numeric_limits<RHS>::max()
        };

        operator std::pair<LHS, RHS>() const {
            return {lhs, rhs};
        }

        constexpr SlotStruct(const LHS lhs, const RHS rhs) : lhs(lhs), rhs(rhs) {}
        constexpr SlotStruct(const std::pair<LHS, RHS>& p) : lhs(p.first), rhs(p.second) {}
    };
    static_assert(sizeof(SlotStruct<uint32_t, uint32_t, uint64_t>) == sizeof(uint64_t));
    static_assert(alignof(SlotStruct<uint32_t, uint32_t, uint64_t>) == alignof(uint64_t));


    using IndexSlot = SlotStruct<uint32_t, uint32_t>;

    struct Hasher {
        constexpr std::uint32_t operator()(const IndexSlot& slot) const {
            return (slot.lhs * 0x9e3779b9u) ^ slot.rhs;
        }
    };

    struct SlotEqual {
        bool operator()(const IndexSlot& lhs, const IndexSlot& rhs) const {
            return lhs.lhs == rhs.lhs && lhs.rhs == rhs.rhs;
        }
    };

    using FixedHashSetSlot = FixedHashSet<IndexSlot, Hasher, SlotEqual>;

}

#endif
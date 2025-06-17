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

#ifndef VALLA_INCLUDE_ROOT_INDEXED_HASH_SET_HPP_
#define VALLA_INCLUDE_ROOT_INDEXED_HASH_SET_HPP_

#include "valla/declarations.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace valla
{
/// @brief `IndexedHashSet` encapsulates a bijective function f : uint32_t -> Index with inverse mapping f^{-1} : Index -> uint32_t
/// where the indices in the image are enumerated 0,1,2,... and so on.
///
/// TODO: think more about how to implement this using a Cleary table!
/// Current understanding: reduces current memory overhead from 5/2 to 1 at the cost of a bidirectional probing search!!!
/// Found some code on this: https://github.com/DaanWoltgens/ClearyCuckooParallel/tree/main

class RootIndexedHashSet
{
public:
    auto insert_slot(uint32_t slot)
    {
        auto result = slot_to_index.emplace(slot, slot_to_index.size());
        if (result.second)
        {
            index_to_slot.push_back(slot);
        }
        return result;
    }

    // Get slot by its index
    uint32_t get_slot(uint32_t index) const
    {
        assert(index < index_to_slot.size() && "Index out of bounds");
        return index_to_slot[index];
    }
    uint32_t get_index(uint32_t slot) const
    {
        assert(slot < slot_to_index.size() && "Index out of bounds");
        return slot_to_index.at(slot);
    }

    bool exists(uint32_t slot) const { return slot_to_index.find(slot) != slot_to_index.end(); }

    size_t size() const { return index_to_slot.size(); }

    // Approximate memory usage (not very exact!)
    size_t get_memory_usage() const
    {
        size_t usage = 0;
        usage += slot_to_index.size() * (sizeof(uint32_t) + sizeof(uint32_t));
        usage += index_to_slot.capacity() * sizeof(uint32_t);
        return usage;
    }

private:
    phmap::flat_hash_map<uint32_t, uint32_t> slot_to_index;
    std::vector<uint32_t> index_to_slot;
};
}

#endif

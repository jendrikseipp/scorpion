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

#ifndef VALLA_INCLUDE_INDEXED_HASH_SET_HPP_
#define VALLA_INCLUDE_INDEXED_HASH_SET_HPP_

#include "valla/declarations.hpp"

#include <parallel_hashmap/phmap.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace valla
{
/// @brief `IndexedHashSet` encapsulates a bijective function f : Slot -> Index with inverse mapping f^{-1} : Index -> Slot
/// where the indices in the image are enumerated 0,1,2,... and so on.
///
/// TODO: think more about how to implement this using a Cleary table!
/// Current understanding: reduces current memory overhead from 5/2 to 1 at the cost of a bidirectional probing search!!!
/// Found some code on this: https://github.com/DaanWoltgens/ClearyCuckooParallel/tree/main
class IndexedHashSet
{
public:
    auto insert_slot(Slot slot)
    {
        const auto result = m_slot_to_index.emplace(slot, m_slot_to_index.size());

        if (result.second)
        {
            m_index_to_slot.push_back(slot);
        }

        return result;
    }

    Slot get_slot(Index index) const
    {
        assert(index < m_index_to_slot.size() && "Index out of bounds");

        return m_index_to_slot[index];
    }

    size_t size() const { return m_index_to_slot.size(); }

    size_t get_memory_usage() const
    {
        size_t usage = 0;

        usage += m_slot_to_index.bucket_count() * (1 + sizeof(Slot) + sizeof(Index));

        usage += m_index_to_slot.capacity() * sizeof(Slot);

        return usage;
    }

    size_t get_occupied_memory_usage() const
    {
        size_t usage = 0;

        usage += m_slot_to_index.size() * (1 + sizeof(Slot) + sizeof(Index));

        usage += m_index_to_slot.size() * sizeof(Slot);

        return usage;
    }

    ~IndexedHashSet() {
        utils::g_log << "State set destroyed, size: " << size() << " entries"<< std::endl;
        utils::g_log << "State set destroyed, size per entry: " << 2 << " blocks"<< std::endl;
        utils::g_log << "State set destroyed, capacity: " << m_slot_to_index.capacity() << " entries" << std::endl;
        utils::g_log << "State set destroyed, byte size: " << static_cast<double>(get_occupied_memory_usage()) / (1024 * 1024) << "MB" << std::endl;
        utils::g_log << "State set destroyed, byte capacity: " << static_cast<double>(get_memory_usage()) / (1024 * 1024) << "MB" << std::endl;
    };

private:
    phmap::flat_hash_map<Slot, Index> m_slot_to_index;
    std::vector<Slot> m_index_to_slot;
};
}

#endif

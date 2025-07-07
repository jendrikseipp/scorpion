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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stack>

namespace valla
{
/// @brief `IndexedHashSet` encapsulates a bijective function f : Slot -> Index with inverse mapping f^{-1} : Index -> Slot
/// where the indices in the image are enumerated 0,1,2,... and so on.
class IndexedHashSet
{
public:
    IndexedHashSet() : m_index_to_slot(), m_uniqueness(0, IndexReferencedSlotHash(m_index_to_slot), IndexReferencedSlotEqualTo(m_index_to_slot)) {}
    // Uncopieable and unmoveable to avoid dangling references of m_index_to_slot in hash and equal_to.
    IndexedHashSet(const IndexedHashSet& other) = delete;
    IndexedHashSet& operator=(const IndexedHashSet& other) = delete;
    IndexedHashSet(IndexedHashSet&& other) = delete;
    IndexedHashSet& operator=(IndexedHashSet&& other) = delete;

    auto insert(SlotStruct<> slot)
    {
        assert(m_uniqueness.size() != std::numeric_limits<Index>::max() && "IndexedHashSet: Index overflow! The maximum number of slots reached.");

        Index index = m_index_to_slot.size();

        m_index_to_slot.push_back(slot);

        const auto result = m_uniqueness.emplace(index);

        if (!result.second)
            m_index_to_slot.pop_back();

        return result;
    }

    SlotStruct<> operator[](Index index) const
    {
        assert(index < m_index_to_slot.size() && "Index out of bounds");

        return m_index_to_slot[index];
    }

    size_t size() const { return m_index_to_slot.size(); }

    size_t get_memory_usage() const
    {
        size_t usage = 0;

        usage += m_uniqueness.bucket_count() * (1 + sizeof(Index));

        usage += m_index_to_slot.capacity() * sizeof(Slot);

        return usage;
    }

    size_t get_occupied_memory_usage() const
    {
        size_t usage = 0;

        usage += m_uniqueness.size() * (1 + sizeof(Index));

        usage += m_index_to_slot.size() * sizeof(Slot);

        return usage;
    }

    ~IndexedHashSet() {
        utils::g_log << "State set destroyed, size: " << size() << " entries"<< std::endl;
        utils::g_log << "State set destroyed, size per entry: " << 2 << " blocks"<< std::endl;
        utils::g_log << "State set destroyed, capacity: " << m_uniqueness.capacity() << " entries" << std::endl;
        utils::g_log << "State set destroyed, byte size: " << static_cast<double>(get_occupied_memory_usage()) / (1024 * 1024) << "MB" << std::endl;
        utils::g_log << "State set destroyed, byte capacity: " << static_cast<double>(get_memory_usage()) / (1024 * 1024) << "MB" << std::endl;
    };
private:
    std::vector<SlotStruct<>> m_index_to_slot;
    phmap::flat_hash_set<Index, IndexReferencedSlotHash, IndexReferencedSlotEqualTo> m_uniqueness;
};

}

#endif
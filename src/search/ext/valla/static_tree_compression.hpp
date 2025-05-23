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

#ifndef VALLA_INCLUDE_TREE_COMPRESSION_HPP_
#define VALLA_INCLUDE_TREE_COMPRESSION_HPP_

#include "valla/declarations.hpp"
#include "valla/indexed_hash_set.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <iostream>
#include <ranges>
#include <stack>

namespace valla::static_tree
{

/// @brief Recursively insert the elements from `it` until `end` into the `table`.
/// @param it points to the first element.
/// @param end points after the last element.
/// @param table is the table to uniquely insert the slots.
/// @return the index of the slot at the root.
template<std::forward_iterator Iterator>
    requires std::same_as<std::iter_value_t<Iterator>, Index>
inline Index insert_recursively(Iterator it, Iterator end, size_t size, IndexedHashSet& table)
{
    /* Base cases */
    if (size == 1)
        return *it;  ///< Skip node creation

    if (size == 2)
        return table.insert_slot(make_slot(*it, *(it + 1))).first->second;

    /* Divide */
    const auto mid = std::bit_floor(size - 1);

    /* Conquer */
    const auto mid_it = it + mid;
    const auto left_index = insert_recursively(it, mid_it, mid, table);
    const auto right_index = insert_recursively(mid_it, end, size - mid, table);

    return table.insert_slot(make_slot(left_index, right_index)).first->second;
}

/// @brief Recursively insert the elements from `it` until `end` into the `table`.
/// @param it points to the first element.
/// @param end points after the last element.
/// @param table is the table to uniquely insert the slots.
/// @return the index of the slot at the root.
template<std::forward_iterator Iterator>
    requires std::same_as<std::iter_value_t<Iterator>, Index>
inline std::pair<unsigned long, bool> emplace_recursively(Iterator it, Iterator end, size_t size, IndexedHashSet& table)
{
    if (size == 1)
        return std::pair{static_cast<size_t>(*it), false};

    if (size == 2){
        auto [iter, inserted] = table.insert_slot(make_slot(*it, *(it + 1)));
        return std::pair{iter->second, inserted};
    }

    /* Divide */
    const auto mid = std::bit_floor(size - 1);

    /* Conquer */
    const auto mid_it = it + mid;
    const auto [left_index, left_inserted] = emplace_recursively(it, mid_it, mid, table);
    const auto [right_index, right_inserted] = emplace_recursively(mid_it, end, size - mid, table);

    auto [iter, inserted] = table.insert_slot(make_slot(left_index, right_index));

    return std::pair{iter->second, left_inserted || right_inserted || inserted};


}

/// @brief Inserts the elements from the given `state` into the `tree_table` and the `root_table`.
/// @param state is the given state.
/// @param tree_table is the tree table whose nodes encode the tree structure without size information.
/// @param root_table is the root_table whose nodes encode the root tree index ONLY.
/// @return A pair (it, bool) where it points to the entry in the root table and bool is true if and only if the state was newly inserted.
template<std::ranges::forward_range Range>
    requires std::same_as<std::ranges::range_value_t<Range>, Index>
auto insert(const Range& state, IndexedHashSet& tree_table, RootIndices& root_table)
{
    // Note: O(1) for random access iterators, and O(N) otherwise by repeatedly calling operator++.
    const auto size = static_cast<size_t>(std::distance(state.begin(), state.end()));

    if (size == 0)                                                     ///< Special case for empty state.
        return std::pair{root_table.size(), false};  ///< Len 0 marks the empty state, the tree index can be arbitrary so we set it to 0.

    if (size == 1) {
        auto [iter, inserted] = tree_table.insert_slot(make_slot(state[0], 0));
        return std::pair{static_cast<size_t>(iter->second), inserted};
    }

    auto [index, inserted] = emplace_recursively(state.begin(), state.end(), size, tree_table);
    if (!inserted && size >= 2)
        return std::pair{index, false};  ///< The state already exists.
    root_table.emplace_back(index);
    return std::pair{root_table.size()-1, inserted};
}

/// @brief Recursively reads the state from the tree induced by the given `index` and the `len`.
/// @param index is the index of the slot in the tree table.
/// @param size is the length of the state that defines the shape of the tree at the index.
/// @param tree_table is the tree table.
/// @param out_state is the output state.
inline void read_state_recursively(Index index, size_t size, const IndexedHashSet& tree_table, State& ref_state)
{
    /* Base case */
    if (size == 1)
    {
        ref_state.push_back(index);
        return;
    }

    const auto [left_index, right_index] = read_slot(tree_table.get_slot(index));

    /* Base case */
    if (size == 2)
    {
        ref_state.push_back(left_index);
        ref_state.push_back(right_index);
        return;
    }

    /* Divide */
    const auto mid = std::bit_floor(size - 1);

    /* Conquer */
    read_state_recursively(left_index, mid, tree_table, ref_state);
    read_state_recursively(right_index, size - mid, tree_table, ref_state);
}

/// @brief Read the `out_state` from the given `tree_index` from the `tree_table`.
/// @param index
/// @param size
/// @param tree_table
/// @param out_state
inline void read_state(Index tree_index, size_t size, const IndexedHashSet& tree_table, State& out_state)
{
    out_state.clear();
    assert(out_state.capacity() >= size);

    if (size == 0)  ///< Special case for empty state.
        return;

    if (size == 1)
    {
        out_state.push_back(read_slot(tree_table.get_slot(tree_index)).first);
        return;
    }


    read_state_recursively(tree_index, size, tree_table, out_state);
}

/// @brief Read the `out_state` from the given `root_index` from the `root_table`.
/// @param root_index is the index of the slot in the root table.
/// @param tree_table is the tree table.
/// @param root_table is the root table.
/// @param out_state is the output state.
inline void read_state(Index root_index, size_t size, const IndexedHashSet& tree_table, const RootIndices& root_table, State& out_state)
{
    /* Observe: a root slot wraps the root tree_index together with the length that defines the tree structure! */
    const auto tree_index = root_table[root_index];

    read_state(tree_index, size, tree_table, out_state);
}

class const_iterator
{
private:
    const IndexedHashSet* m_tree_table;

    struct Entry
    {
        Index m_index;
        Index m_size;
    };

    std::stack<Entry> m_stack;

    Index m_value;

    static constexpr const Index END_POS = Index(-1);

    void advance()
    {
        while (!m_stack.empty())
        {
            auto entry = m_stack.top();
            m_stack.pop();

            if (entry.m_size == 1)
            {
                m_value = entry.m_index;
                return;
            }

            const auto [left, right] = read_slot(m_tree_table->get_slot(entry.m_index));

            Index mid = std::bit_floor(entry.m_size - 1);

            // Emplace right first to ensure left is visited first in dfs.
            m_stack.emplace(right, entry.m_size - mid);
            m_stack.emplace(left, mid);
        }

        m_value = END_POS;
    }

public:
    using difference_type = std::ptrdiff_t;
    using value_type = Index;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    const_iterator() : m_tree_table(nullptr), m_stack(), m_value(END_POS) {}
    const_iterator(const IndexedHashSet* tree_table, size_t tree_idx, size_t size, bool begin) : m_tree_table(tree_table), m_stack(), m_value(END_POS)
    {
        if (begin)
        {
            if (size > 0)  ///< Push to stack only if there leafs
            {
                m_stack.emplace(tree_idx, size);
                advance();
            }
        }
    }
    value_type operator*() const { return m_value; }
    const_iterator& operator++()
    {
        advance();
        return *this;
    }
    const_iterator operator++(int)
    {
        auto it = *this;
        ++it;
        return it;
    }
    bool operator==(const const_iterator& other) const { return m_value == other.m_value; }
    bool operator!=(const const_iterator& other) const { return !(*this == other); }
};

inline const_iterator begin(size_t tree_index, size_t size, const IndexedHashSet& tree_table) { return const_iterator(&tree_table, tree_index, size, true); }

inline const_iterator end() { return const_iterator(); }

}

#endif
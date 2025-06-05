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

#ifndef VALLA_INCLUDE_FIXED_TREE_COMPRESSION_HPP_
#define VALLA_INCLUDE_FIXED_TREE_COMPRESSION_HPP_

#include "valla/declarations.hpp"
#include "valla/indexed_hash_set.hpp"
#include "valla/root_indexed_hash_set.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <iostream>
#include <ranges>
#include <stack>

#include "fixed_hash_set.hpp"
#include "hash.h"

namespace valla::fixed_tree
{

    inline auto calc_mid(size_t size)
    {
        return size / 2 + (size % 2);
    }

    template<typename LHS, typename RHS>
    struct Slot {
        LHS lhs;
        RHS rhs;
        constexpr bool operator==(const Slot&) const = default;
    };
    constexpr Slot SlotSentinel{std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};


    using IndexSlot = Slot<uint32_t, uint32_t>;

    struct Hasher {
        std::size_t operator()(const IndexSlot& slot) const {
            utils::HashState hash_state;
            hash_state.feed(slot.lhs);
            hash_state.feed(slot.rhs);
            return hash_state.get_hash64();
        }
    };

    struct SlotEqual {
        bool operator()(const IndexSlot& lhs, const IndexSlot& rhs) const {
            return lhs.lhs == rhs.lhs && lhs.rhs == rhs.rhs;
        }
    };

    using FixedHashSetSlot = FixedHashSet<IndexSlot, SlotSentinel, Hasher, SlotEqual>;

/// @brief Recursively insert the elements from `it` until `end` into the `table`.
/// @param it points to the first element.
/// @param end points after the last element.
/// @param table is the table to uniquely insert the slots.
/// @return the index of the slot at the root.
    template<std::forward_iterator Iterator>
    requires std::same_as<std::iter_value_t<Iterator>, Index>
inline Index insert_recursively(Iterator it, Iterator end, size_t size,
    FixedHashSetSlot table)
{
    /* Base cases */
    if (size == 1)
        return *it;  ///< Skip node creation

    if (size == 2)
        return table.insert({*it, *(it + 1)}).first;

    /* Divide */
    const auto mid = calc_mid(size);

    /* Conquer */
    const auto mid_it = it + mid;
    const auto left_index = insert_recursively(it, mid_it, mid, table);
    const auto right_index = insert_recursively(mid_it, end, size - mid, table);

    return table.insert({left_index, right_index}).first;
}

/// @brief Recursively insert the elements from `it` until `end` into the `table`.
/// @param it points to the first element.
/// @param end points after the last element.
/// @param table is the table to uniquely insert the slots.
/// @return the index of the slot at the root.
template< std::forward_iterator Iterator>
    requires std::same_as<std::iter_value_t<Iterator>, Index>
inline std::pair<unsigned long, bool> emplace_recursively(Iterator it, Iterator end, size_t size, FixedHashSetSlot& table)
{
    if (size == 1)
        return std::pair{static_cast<size_t>(*it), false};

    if (size == 2){
        auto [idx, inserted] = table.insert({*it, *(it + 1)});
        return std::pair{idx, inserted};
    }

    /* Divide */
    const auto mid = calc_mid(size);

    /* Conquer */
    const auto mid_it = it + mid;
    const auto [left_index, left_inserted] = emplace_recursively(it, mid_it, mid, table);
    const auto [right_index, right_inserted] = emplace_recursively(mid_it, end, size - mid, table);

    auto [idx, inserted] = table.insert({left_index, right_index});

    return std::pair{idx, inserted};


}

    /// @brief Inserts the elements from the given `state` into the `tree_table` and the `root_table`.
    /// @param state is the given state.
    /// @param tree_table is the tree table whose nodes encode the tree structure without size information.
    /// @param root_table is the root_table whose nodes encode the root tree index ONLY.
    /// @return A pair (it, bool) where it points to the entry in the root table and bool is true if and only if the state was newly inserted.
    template< std::ranges::forward_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Index>
    auto insert(const Range& state, FixedHashSetSlot& tree_table)
    {
        // Note: O(1) for random access iterators, and O(N) otherwise by repeatedly calling operator++.
        const auto size = static_cast<size_t>(std::distance(state.begin(), state.end()));

        if (size == 0)                                                     ///< Special case for empty state.
            return std::pair{tree_table.size(), false};  ///< Len 0 marks the empty state, the tree index can be arbitrary so we set it to 0.

        if (size == 1)  ///< Special case for singletons.
        {
            auto [idx, inserted] = tree_table.insert({*state.begin(), 0});
            return std::pair{idx, !inserted};  ///< The state already exists.
        }

        auto [idx, inserted] = emplace_recursively(state.begin(), state.end(), size, tree_table);
        return std::pair{idx, !inserted};  ///< The state already exists.
    }

/// @brief Recursively reads the state from the tree induced by the given `index` and the `len`.
/// @param index is the index of the slot in the tree table.
/// @param size is the length of the state that defines the shape of the tree at the index.
/// @param tree_table is the tree table.
/// @param out_state is the output state.
template <typename FixedHashSetSlot>
inline void read_state_recursively(Index index, size_t size, const FixedHashSetSlot& tree_table, State& ref_state)
{
    /* Base case */
    if (size == 1)
    {
        ref_state.push_back(index);
        return;
    }

    const auto [left_index, right_index] = tree_table.get(index);

    /* Base case */
    if (size == 2)
    {
        ref_state.push_back(left_index);
        ref_state.push_back(right_index);
        return;
    }

    /* Divide */
    const auto mid = calc_mid(size);

    /* Conquer */
    read_state_recursively(left_index, mid, tree_table, ref_state);
    read_state_recursively(right_index, size - mid, tree_table, ref_state);
}

/// @brief Read the `out_state` from the given `tree_index` from the `tree_table`.
/// @param index
/// @param size
/// @param tree_table
/// @param out_state
template <typename FixedHashSetSlot>
inline void read_state(Index tree_index, size_t size, const FixedHashSetSlot& tree_table, State& out_state)
{
    out_state.clear();
    assert(out_state.capacity() >= size);

    if (size == 0)  ///< Special case for empty state.
        return;

    if (size == 1)  ///< Special case for singletons.
    {
        out_state.push_back(tree_table.get(tree_index).lhs);
        return;
    }

    read_state_recursively(tree_index, size, tree_table, out_state);
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

            Index mid = calc_mid(entry.m_size);

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
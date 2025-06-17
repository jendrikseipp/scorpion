#pragma once
#include <vector>
#include <queue>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <utility>

#include "declarations.hpp"
#include "dynamic_bitset.h"
#include "bitset_pool.hpp"
#include "huffman_tree_compression.hpp"
#include "subtree_split_index.h"
#include "utils.h"
#include "../../per_state_bitset.h"

namespace valla::canonical_fixed_tree {

    /// @brief Recursively insert the elements from `it` until `end` into the `table`.
    /// @param ordering
    /// @param mid_spans
    /// @param it points to the first element.
    /// @param end points after the last element.
    /// @param table is the table to uniquely insert the slots.
    /// @return the index of the slot at the root.
    template<std::forward_iterator Iterator>
        requires std::same_as<std::iter_value_t<Iterator>, Index>
    inline std::pair<unsigned long, bool> emplace_recursively(Iterator state_begin, Iterator state_end,
                                                              Bitset ordering,
                                                              const std::vector<SubtreeSplitInfo> &mid_spans,
                                                              const std::size_t pos,
                                                              FixedHashSetSlot &table) {
        auto size = std::distance(state_begin, state_end);
        if (size == 1)
            return std::pair{static_cast<size_t>(*state_begin), false};

        if (size == 2) {
            std::pair slot{*state_begin, *(state_begin + 1)};
            bool swapped = order_pair(slot);
            if (swapped) ordering.set(pos);

            auto [idx, inserted] = table.insert(slot);
            return std::pair{idx, inserted};
        }

        /* Divide */
        const auto [mid, next_mid] = mid_spans[pos];
        /* Conquer */
        const auto mid_it = state_begin + mid;
        const auto [left_index, left_inserted] = emplace_recursively(state_begin, mid_it, ordering, mid_spans, pos + 1,
                                                                     table);
        const auto [right_index, right_inserted] = emplace_recursively(mid_it, state_end, ordering, mid_spans, next_mid,
                                                                       table);

        std::pair<Index, Index> slot = {left_index, right_index};
        bool swapped = order_pair(slot);
        if (swapped) ordering.set(pos);

        auto [idx, inserted] = table.insert(slot);

        return std::pair{idx, inserted};
    }

    /// @brief Inserts the elements from the given `state` into the `tree_table` and the `root_table`.
    /// @param state is the given state.
    /// @param tree_table is the tree table whose nodes encode the tree structure without size information.
    /// @param root_table is the root_table whose nodes encode the root tree index ONLY.
    /// @return A pair (it, bool) where it points to the entry in the root table and bool is true if and only if the state was newly inserted.
    template<std::ranges::forward_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Index>
    std::pair<SlotStruct<>, bool> insert(const Range &state, const std::vector<SubtreeSplitInfo> &mid_spans,
                                         FixedHashSetSlot &tree_table,
                                         BitsetPool &pool, BitsetRepository &repo) {
        // Note: O(1) for random access iterators, and O(N) otherwise by repeatedly calling operator++.
        const size_t size = std::distance(state.begin(), state.end());

        if (size == 0) ///< Special case for empty state.
            return {
                SlotStruct<>{std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()}, false
            };


        if (size == 1)
        {
            auto [idx, inserted] = tree_table.insert({*state.begin(), 0});
            return {
                    {std::numeric_limits<uint32_t>::max(), static_cast<uint32_t>(idx)}, !inserted
            };
        }


        auto ordering = pool.allocate(std::bit_ceil(mid_spans.size()));
        auto [idx, inserted] = emplace_recursively(state.begin(), state.end(), ordering, mid_spans, 0, tree_table);

        // Undo the bitset allocation when proven that an identical bitset already exists
        const auto result = repo.insert(ordering);
        if (!result.second) {
            pool.pop_allocation();
        }

        SlotStruct<> root_state{idx, (*result.first)->get_index()};
        return {root_state, !(inserted || result.second)};
    }


    /// @brief Recursively reads the state from the tree induced by the given `index` and the `len`.
    /// @param index is the index of the slot in the tree table.
    /// @param size is the length of the state that defines the shape of the tree at the index.
    /// @param tree_table is the tree table.
    /// @param out_state is the output state.
    template<typename FixedHashSetSlot>
    inline void read_state_recursively(Index index, const Bitset &ordering, size_t size,
                                       size_t pos, const std::vector<SubtreeSplitInfo> &mid_spans,
                                       const FixedHashSetSlot &tree_table, State &ref_state) {
        /* Base case */
        if (size == 1) {
            ref_state.push_back(index);
            return;
        }

        auto [left_index, right_index] = tree_table.get(index);
        const auto must_swap = ordering.get(pos);

        if (must_swap) {
            std::swap(left_index, right_index);
        }


        /* Base case */
        if (size == 2) {
            ref_state.push_back(left_index);
            ref_state.push_back(right_index);
            return;
        }

        /* Divide */
        const auto [mid, next_mid] = mid_spans[pos];

        /* Conquer */
        read_state_recursively(left_index, ordering, mid, pos + 1, mid_spans, tree_table, ref_state);
        read_state_recursively(right_index, ordering, size - mid, next_mid, mid_spans, tree_table, ref_state);
    }

    /// Canonical *iterative* read_state from table and MergeSchedule.
    /// Assumes leaves are filled in variable order.
    /// @param out_state: must have capacity >= variable_order.size()
    template<typename FixedHashSetSlot>
    inline void read_state(Index tree_index,
                           const Bitset &ordering,
                           const size_t size,
                           const std::vector<SubtreeSplitInfo> &mid_spans, const FixedHashSetSlot &tree_table,
                           State &out_state) {
        out_state.clear();
        assert(out_state.capacity() >= size);

        if (size == 0) ///< Special case for empty state.
            return;

        if (size == 1) ///< Special case for singletons.
        {
            out_state.push_back(tree_table.get(tree_index).lhs);
            return;
        }

        read_state_recursively(tree_index, ordering, size, 0, mid_spans, tree_table, out_state);
    }

} // namespace valla::huffman

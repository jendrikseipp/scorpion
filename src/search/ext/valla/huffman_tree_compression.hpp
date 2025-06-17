#pragma once
#include <vector>
#include <queue>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "declarations.hpp"
#include "dynamic_bitset.h"
#include "subtree_split_index.h"


namespace valla::huffman_tree {

    /// @brief Recursively insert the elements from `it` until `end` into the `table`.
    ///
    /// Recursively splits the state according to the merge schedule and inserts each subtree into
    /// the hash set table, returning the index of the root slot.
    ///
    /// @tparam Iterator Forward iterator over Index values.
    /// @param state_begin Iterator to the first element.
    /// @param state_end Iterator past the last element.
    /// @param mid_spans Precomputed subtree split information.
    /// @param pos Current position in mid_spans.
    /// @param table The hash set table for unique subtree storage.
    /// @return A pair (index, bool) where index is the root slot and bool is true if newly inserted.
    template<std::forward_iterator Iterator>
        requires std::same_as<std::iter_value_t<Iterator>, Index>
    inline auto emplace_recursively(Iterator state_begin, Iterator state_end,
                                                              const std::vector<SubtreeSplitInfo> &mid_spans,
                                                              const std::size_t pos,
                                                              FixedHashSetSlot &table) {
        auto size = std::distance(state_begin, state_end);
        if (size == 1)
            return std::pair{static_cast<uint32_t>(*state_begin), false};

        if (size == 2) {
            auto [idx, inserted] = table.insert({*state_begin, *(state_begin + 1)});
            return std::pair{idx, inserted};
        }

        /* Divide */
        const auto [mid, tree_mid] = mid_spans[pos];
        /* Conquer */
        const auto mid_it = state_begin + mid;
        const auto [left_index, left_inserted] = emplace_recursively(state_begin, mid_it, mid_spans, pos + 1, table);
        const auto [right_index, right_inserted] = emplace_recursively(mid_it, state_end, mid_spans, tree_mid, table);

        auto [idx, inserted] = table.insert({left_index, right_index});

        return std::pair{idx, inserted};
    }

    /// @brief Inserts a state sequence into the tree table using the provided merge schedule.
    ///
    /// This function inserts the given state (a sequence of variable indices) into the tree_table,
    /// which encodes the tree structure for compressed state representation. The insertion uses the
    /// precomputed mid_spans (from the merge schedule) to recursively split and combine the state.
    ///
    /// @tparam Range A forward range whose value type is Index.
    /// @param state The state sequence to insert.
    /// @param mid_spans The precomputed subtree split information (from the merge schedule).
    /// @param tree_table The hash set table encoding the tree structure.
    /// @return A pair (index, bool) where index is the root index in the tree table, and bool is true if the state was newly inserted.
    template<std::ranges::forward_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Index>
    auto insert(const Range &state, const std::vector<SubtreeSplitInfo> &mid_spans, FixedHashSetSlot &tree_table) {
        // Note: O(1) for random access iterators, and O(N) otherwise by repeatedly calling operator++.
        const auto size = std::distance(state.begin(), state.end());

        if (size == 0) ///< Special case for empty state.
            return std::pair{tree_table.size(), false};
        ///< Len 0 marks the empty state, the tree index can be arbitrary so we set it to 0.

        if (size == 1) ///< Special case for singletons.
        {
            auto [idx, inserted] = tree_table.insert({*state.begin(), 0});
            return std::pair{idx, !inserted}; ///< The state already exists.
        }

        auto [idx, inserted] = emplace_recursively(state.begin(), state.end(), mid_spans, 0, tree_table);
        return std::pair{idx, !inserted}; ///< The state already exists.
    }


    /// @brief Recursively reconstructs a state from the tree table using the merge schedule.
    ///
    /// This function reads and reconstructs the original state sequence from the compressed tree
    /// representation, starting from the given index and using the merge schedule's mid_spans to
    /// guide the recursive traversal.
    ///
    /// @tparam FixedHashSetSlot The type of the tree table.
    /// @param index The root index in the tree table.
    /// @param size The number of variables in the state.
    /// @param pos The current position in the mid_spans vector.
    /// @param mid_spans The precomputed subtree split information (from the merge schedule).
    /// @param tree_table The hash set table encoding the tree structure.
    /// @param ref_state The output vector to store the reconstructed state.
    template<typename FixedHashSetSlot>
    inline void read_state_recursively(Index index, size_t size,
                                       size_t pos, const std::vector<SubtreeSplitInfo> &mid_spans,
                                       const FixedHashSetSlot &tree_table, State &ref_state) {
        /* Base case */
        if (size == 1) {
            ref_state.push_back(index);
            return;
        }

        const auto [left_index, right_index] = tree_table.get(index);

        /* Base case */
        if (size == 2) {
            ref_state.push_back(left_index);
            ref_state.push_back(right_index);
            return;
        }

        /* Divide */
        const auto [mid, tree_mid] = mid_spans[pos];

        /* Conquer */
        read_state_recursively(left_index, mid, pos + 1, mid_spans, tree_table, ref_state);
        read_state_recursively(right_index, size - mid, tree_mid, mid_spans, tree_table, ref_state);
    }

    /// Canonical *iterative* read_state from table and MergeSchedule.
    /// Assumes leaves are filled in variable order.
    /// @param out_state: must have capacity >= variable_order.size()
    template<typename FixedHashSetSlot>
    inline void read_state(Index tree_index, const size_t size, const std::vector<SubtreeSplitInfo> &mid_spans,
                           const FixedHashSetSlot &tree_table, State &out_state) {
        out_state.clear();
        assert(out_state.capacity() >= size);

        if (size == 0) ///< Special case for empty state.
            return;

        if (size == 1) ///< Special case for singletons.
        {
            out_state.push_back(tree_table.get(tree_index).lhs);
            return;
        }

        read_state_recursively(tree_index, size, 0, mid_spans, tree_table, out_state);
    }
} // namespace valla::huffman

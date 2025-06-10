#pragma once
#include <vector>
#include <queue>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "dynamic_bitset.h"

namespace valla::huffman_tree
{

// --- Merge Tree Construction Utilities ---


/// Bulk precompute all calc_mid(pos, traversal) results, for all positions.
/// Returns a vector such that for any valid pos:
///   all_calc_mids[pos] == {vars_to_left, mid_tree} == calc_mid(pos, traversal)
inline std::vector<std::pair<std::size_t, std::size_t>>
precompute_all_calc_mids(const dynamic_bitset::DynamicBitset<>& traversal)
{
    std::vector<std::pair<std::size_t, std::size_t>> all_calc_mids;
    all_calc_mids.reserve(traversal.size());

    for (std::size_t pos = 0; pos < traversal.size()-1; ++pos) {
        // Only compute where there's actually a node (bitset length==traversal size)
        std::size_t mid_tree = pos + 1;
        unsigned int depth = 1;
        unsigned int vars_to_left = 0;
        while (depth != 0) {
            depth += traversal[mid_tree] == 0 ? 1 : -1;
            vars_to_left += traversal[mid_tree];
            ++mid_tree;
        }
        all_calc_mids.emplace_back(vars_to_left, mid_tree);
    }
    return all_calc_mids;
}



struct MergeNode {
    std::size_t left, right;
};

struct MergeSchedule {
    std::vector<std::size_t> variable_order;    // Canonical order for leaf variables
    dynamic_bitset::DynamicBitset<> traversal;  // Preorder: 1=merge, 0=leaf
    std::vector<std::pair<size_t, size_t>> traversal_splits;  // Preorder: 1=merge, 0=leaf
};

// Internal structure for queueing subtrees by cost
struct QueueElem {
    std::size_t cost, idx;
    bool is_leaf = false; // true if this is a leaf node (variable)
    std::unique_ptr<QueueElem> left = nullptr;
    std::unique_ptr<QueueElem> right = nullptr;
};


// Recursively derive canonical variable order and traversal bitvector (preorder)
// Handles unbalanced/degenerate "optimal product" trees.
// Returns: (variable_order, traversal_bits)
inline std::pair<std::vector<std::size_t>, std::vector<bool>>
recursive_tree_dfs(const QueueElem* node)
{
    std::vector<std::size_t> variable_order;
    std::vector<bool> traversal_bits;

    // Local lambda for recursion
    std::function<void(const QueueElem*)> dfs = [&](const QueueElem* cur) {
        traversal_bits.push_back(cur->is_leaf);
        if (cur->is_leaf) {
            variable_order.push_back(cur->idx);
            return;
        }
        if (cur->left) dfs(cur->left.get());
        if (cur->right) dfs(cur->right.get());
    };

    dfs(node);
    return {std::move(variable_order), std::move(traversal_bits)};
}


inline std::unique_ptr<QueueElem> compute_merge_tree(const std::vector<std::size_t>& costs) {
    using PQElem = std::unique_ptr<QueueElem>;

    struct Compare {
        bool operator()(const PQElem& a, const PQElem& b) const {
            if (a->is_leaf && !b->is_leaf) {
                return false; // Compare by index for leaf nodes
            }
            if (a->cost == b->cost) {
                return a->idx > b->idx;
            }
            return a->cost > b->cost;
        }
    };

    using PQ = std::priority_queue<PQElem, std::vector<PQElem>, Compare>;
    PQ pq;
    for (std::size_t i = 0; i < costs.size(); ++i) {
        pq.push(std::make_unique<QueueElem>(QueueElem{costs[i], i, true}));
    }

    std::vector<MergeNode> merges;

    while (pq.size() > 1) {
        auto a = std::move(const_cast<PQElem&>(pq.top())); pq.pop();
        auto b = std::move(const_cast<PQElem&>(pq.top())); pq.pop();

        merges.push_back({a->idx, b->idx});
        pq.push(std::make_unique<QueueElem>(QueueElem{
            a->cost * b->cost,
            merges.size() - 1,
            false,
            std::move(a),
            std::move(b)
        }));
    }

    return std::move(const_cast<PQElem&>(pq.top()));
}
/// Compute optimal merge schedule (based on domain size cost)
/// @param domain_sizes: domain size for each variable, already sorted
/// @returns MergeSchedule: plan to combine nodes, canonical variable order, bitvector
inline MergeSchedule compute_merge_schedule(const std::vector<std::size_t>& domain_sizes)
{
    auto merge_tree = compute_merge_tree(domain_sizes);
    const auto& [variable_order_, traversal_bits_] = recursive_tree_dfs(merge_tree.get());


    dynamic_bitset::DynamicBitset<> traversal(traversal_bits_.size());
    for (auto i = 0; i < traversal_bits_.size(); ++i) {
        if (traversal_bits_[i])
            traversal.set(i);
    }

    auto mid_spans = precompute_all_calc_mids(traversal);

    return MergeSchedule{ std::move(variable_order_), std::move(traversal) , std::move(mid_spans)};
}

/// @brief Recursively insert the elements from `it` until `end` into the `table`.
/// @param it points to the first element.
/// @param end points after the last element.
/// @param table is the table to uniquely insert the slots.
/// @return the index of the slot at the root.
template< std::forward_iterator Iterator>
    requires std::same_as<std::iter_value_t<Iterator>, Index>
    inline std::pair<unsigned long, bool> emplace_recursively(Iterator state_begin, Iterator state_end,
           const std::vector<std::pair<size_t, size_t>>& mid_spans,
           const std::size_t pos,
           FixedHashSetSlot& table)
{
    auto size = std::distance(state_begin, state_end);
    if (size == 1)
        return std::pair{static_cast<size_t>(*state_begin), false};

    if (size == 2){
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

    /// @brief Inserts the elements from the given `state` into the `tree_table` and the `root_table`.
    /// @param state is the given state.
    /// @param tree_table is the tree table whose nodes encode the tree structure without size information.
    /// @param root_table is the root_table whose nodes encode the root tree index ONLY.
    /// @return A pair (it, bool) where it points to the entry in the root table and bool is true if and only if the state was newly inserted.
    template< std::ranges::forward_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Index>
    auto insert(const Range& state, const std::vector<std::pair<size_t, size_t>>& mid_spans, FixedHashSetSlot& tree_table)
    {
        // Note: O(1) for random access iterators, and O(N) otherwise by repeatedly calling operator++.
        const auto size = std::distance(state.begin(), state.end());

        if (size == 0)                                                     ///< Special case for empty state.
            return std::pair{tree_table.size(), false};  ///< Len 0 marks the empty state, the tree index can be arbitrary so we set it to 0.

        if (size == 1)  ///< Special case for singletons.
        {
            auto [idx, inserted] = tree_table.insert({*state.begin(), 0});
            return std::pair{idx, !inserted};  ///< The state already exists.
        }

        auto [idx, inserted] = emplace_recursively(state.begin(), state.end(), mid_spans, 0, tree_table);
        return std::pair{idx, !inserted};  ///< The state already exists.
    }


    /// @brief Recursively reads the state from the tree induced by the given `index` and the `len`.
    /// @param index is the index of the slot in the tree table.
    /// @param size is the length of the state that defines the shape of the tree at the index.
    /// @param tree_table is the tree table.
    /// @param out_state is the output state.
    template <typename FixedHashSetSlot>
    inline void read_state_recursively(Index index, size_t size,
        size_t pos, const std::vector<std::pair<size_t, size_t>>& mid_spans,
        const FixedHashSetSlot& tree_table, State& ref_state)
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
    const auto [mid, tree_mid] = mid_spans[pos];

    /* Conquer */
    read_state_recursively(left_index, mid, pos+1, mid_spans, tree_table, ref_state);
    read_state_recursively(right_index, size - mid, tree_mid, mid_spans, tree_table, ref_state);
}

/// Canonical *iterative* read_state from table and MergeSchedule.
/// Assumes leaves are filled in variable order.
/// @param out_state: must have capacity >= variable_order.size()
template <typename FixedHashSetSlot>
inline void read_state(Index tree_index, const size_t size, const std::vector<std::pair<size_t, size_t>>& mid_spans, const FixedHashSetSlot& tree_table, State& out_state)
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

    read_state_recursively(tree_index, size, 0, mid_spans, tree_table, out_state);
}

} // namespace valla::huffman
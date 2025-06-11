#pragma once
#include <vector>
#include <queue>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "declarations.hpp"
#include "dynamic_bitset.h"


namespace valla::huffman_tree {
    // --- Merge Tree Construction Utilities ---


    /// @brief Precompute all subtree split information for a given traversal bitset.
    ///
    /// For each position in the traversal, computes the number of variables to the left and the
    /// position of the mid-tree, storing the result as SubtreeSplitInfo. Used to accelerate
    /// recursive tree operations.
    ///
    /// @param traversal The bitset representing the tree traversal (preorder, leaf=1, internal=0).
    /// @return A vector of SubtreeSplitInfo for each position in the traversal.
    inline std::vector<SubtreeSplitInfo>
    precompute_all_calc_mids(const dynamic_bitset::DynamicBitset<> &traversal) {
        std::vector<SubtreeSplitInfo> all_calc_mids;
        all_calc_mids.reserve(traversal.size());

        for (std::size_t pos = 0; pos < traversal.size() - 1; ++pos) {
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


    // Internal structure for queueing subtrees by cost
    struct QueueElem {
        std::size_t cost, idx;
        bool is_leaf = false; // true if this is a leaf node (variable)
        std::unique_ptr<QueueElem> left = nullptr;
        std::unique_ptr<QueueElem> right = nullptr;
    };

    /// @brief Recursively derive canonical variable order and traversal bitvector (preorder).
    ///
    /// Traverses the merge tree to produce the canonical variable order and the traversal bitvector
    /// (preorder, leaf=1, internal=0). Handles unbalanced or degenerate trees.
    ///
    /// @param node The root of the merge tree.
    /// @return A pair of (variable_order, traversal_bits).
    inline std::pair<std::vector<unsigned int>, std::vector<bool> >
    recursive_tree_dfs(const QueueElem *node) {
        std::vector<unsigned int> variable_order;
        std::vector<bool> traversal_bits;

        // Local lambda for recursion
        std::function<void(const QueueElem *)> dfs = [&](const QueueElem *cur) {
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

    /// @brief Default merge strategy for building the merge tree.
    ///
    /// Compares two QueueElem nodes by leaf, preferring low cost nodes and lower indices for tie-breaking.
    ///
    /// @param a First node.
    /// @param b Second node.
    /// @return True if a should come after b in the priority queue.
    inline bool default_merge_strategy(const QueueElem *a, const QueueElem *b) {
        if (a->is_leaf && !b->is_leaf) {
            return false;
        }
        if (a->cost == b->cost) {
            return a->idx > b->idx;
        }
        return a->cost > b->cost;
    }

    /// @brief Compute the merge tree for a set of costs using a merge strategy.
    ///
    /// Builds a binary tree by repeatedly merging the lowest-cost nodes according to the merge strategy.
    ///
    /// @param costs The cost for each variable (e.g., domain sizes).
    /// @param merge_strategy Function to determine merge order (default: default_merge_strategy).
    /// @return The root of the constructed merge tree.
    inline std::unique_ptr<QueueElem> compute_merge_tree(
        const std::vector<std::size_t> &costs,
        const std::function<bool(const QueueElem *, const QueueElem *)> &merge_strategy = default_merge_strategy) {
        // Priority queue comparison
        using PQElem = std::unique_ptr<QueueElem>;
        struct Compare {
            const std::function<bool(const QueueElem *, const QueueElem *)> &merge_strategy;

            Compare(const std::function<bool(const QueueElem *, const QueueElem *)> &ms) : merge_strategy(ms) {
            }

            bool operator()(const PQElem &a, const PQElem &b) const {
                return merge_strategy(a.get(), b.get());
            }
        };

        // Priority queue to hold the subtrees
        using PQ = std::priority_queue<PQElem, std::vector<PQElem>, Compare>;
        PQ pq{Compare(merge_strategy)};
        for (std::size_t i = 0; i < costs.size(); ++i) {
            pq.push(std::make_unique<QueueElem>(QueueElem{costs[i], i, true}));
        }


        size_t next_idx = costs.size();
        while (pq.size() > 1) {
            auto a = std::move(const_cast<PQElem &>(pq.top()));
            pq.pop();
            auto b = std::move(const_cast<PQElem &>(pq.top()));
            pq.pop();

            pq.push(std::make_unique<QueueElem>(QueueElem{
                a->cost * b->cost,
                next_idx++,
                false,
                std::move(a),
                std::move(b)
            }));
        }

        return std::move(const_cast<PQElem &>(pq.top()));
    }

    /// @brief Compute the optimal merge schedule for a set of domain sizes.
    ///
    /// Uses the merge tree to determine the canonical variable order, traversal bitvector, and
    /// precomputed subtree splits for efficient state compression.
    ///
    /// @param domain_sizes The domain size for each variable (should be sorted).
    /// @return The computed MergeSchedule (variable order, traversal, mid_spans).
    inline MergeSchedule compute_merge_schedule(const std::vector<size_t> &domain_sizes) {
        auto merge_tree = compute_merge_tree(domain_sizes);
        const auto &[variable_order_, traversal_bits_] = recursive_tree_dfs(merge_tree.get());


        dynamic_bitset::DynamicBitset<> traversal(traversal_bits_.size());
        for (auto i = 0; i < traversal_bits_.size(); ++i) {
            if (traversal_bits_[i])
                traversal.set(i);
        }

        auto mid_spans = precompute_all_calc_mids(traversal);

        return MergeSchedule{variable_order_, std::move(traversal), std::move(mid_spans)};
    }

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
    inline std::pair<unsigned long, bool> emplace_recursively(Iterator state_begin, Iterator state_end,
                                                              const std::vector<SubtreeSplitInfo> &mid_spans,
                                                              const std::size_t pos,
                                                              FixedHashSetSlot &table) {
        auto size = std::distance(state_begin, state_end);
        if (size == 1)
            return std::pair{static_cast<size_t>(*state_begin), false};

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

//
// Created by workbox on 6/17/25.
//

#ifndef SUBTREE_SPLIT_INDEX_H
#define SUBTREE_SPLIT_INDEX_H
#include <functional>
#include <queue>
#include <vector>

#include "declarations.hpp"

namespace valla {


    struct SubtreeSplitInfo {
        size_t var_lhs;
        size_t rhs_index;
    };

    struct MergeSchedule {
        std::vector<unsigned int> variable_order;    // Canonical order for leaf variables
        dynamic_bitset::DynamicBitset<> traversal;  // Preorder: 1=merge, 0=leaf
        std::vector<SubtreeSplitInfo> traversal_splits;  // Preorder: 1=merge, 0=leaf

        size_t bit_size() const {
            return traversal.size();
        }

        size_t num_variables() const {
            return variable_order.size();
        }

        explicit operator const std::vector<SubtreeSplitInfo>&() const { return traversal_splits; }
        explicit operator const dynamic_bitset::DynamicBitset<>&() const { return traversal; }

    };

    struct MergeNode {
        std::size_t left, right;
    };


    // Internal structure for queueing subtrees by cost
    struct QueueElem {
        std::size_t cost, idx;
        bool is_leaf = false; // true if this is a leaf node (variable)
        std::unique_ptr<QueueElem> left = nullptr;
        std::unique_ptr<QueueElem> right = nullptr;
    };
    using MergeStrategy = std::function<bool(const QueueElem *, const QueueElem *)>;

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


    template<class Bitset>
        requires requires(const Bitset &b, std::size_t i) { b[i]; b.size(); }
    inline std::vector<SubtreeSplitInfo>
    precompute_all_calc_mids(const Bitset &traversal) {
        // Build a mapping from traversal index -> internal node index (if internal)
        // -1 for leaves, or you could compact mapping.
        std::vector<int> traversal_to_info_idx(traversal.size(), -1);
        int internal_node_count = 0;
        for (std::size_t i = 0; i < traversal.size(); ++i) {
            if (traversal[i] == 0) {
                traversal_to_info_idx[i] = internal_node_count++;
            }
        }
        std::vector<SubtreeSplitInfo> all_calc_mids;
        all_calc_mids.reserve(internal_node_count);

        for (std::size_t pos = 0; pos < traversal.size() - 1; ++pos) {
            if (traversal[pos] == 0) {
                // Only for internal nodes
                std::size_t rhs_tree = pos + 1;
                unsigned int depth = 1;
                unsigned int vars_to_left = 0;
                while (depth != 0) {
                    depth += (traversal[rhs_tree] == 0) ? 1 : -1;
                    vars_to_left += traversal[rhs_tree];
                    ++rhs_tree;
                }
                // Fix: map traversal index (rhs_tree) to split info index
                // Skip leaves; so if right child is at a leaf, you may want a "null" index or sentinel.
                int right_subtree_info_idx = (rhs_tree < traversal.size() && traversal[rhs_tree] == 0)
                                                 ? traversal_to_info_idx[rhs_tree]
                                                 : -1; // Or whatever sentinel you want if right subtree is a leaf

                all_calc_mids.emplace_back(vars_to_left, right_subtree_info_idx);
            }
        }
        return all_calc_mids;
    }


    // Recursively derive canonical variable order and traversal bitvector (preorder)
    // Handles unbalanced/degenerate "optimal product" trees.
    // Returns: (variable_order, traversal_bits)
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

    /// @brief Compute the merge tree for a set of costs using a merge strategy.
    ///
    /// Builds a binary tree by repeatedly merging the lowest-cost nodes according to the merge strategy.
    ///
    /// @param costs The cost for each variable (e.g., domain sizes).
    /// @param merge_strategy Function to determine merge order (default: default_merge_strategy).
    /// @return The root of the constructed merge tree.
    inline std::unique_ptr<QueueElem> compute_merge_tree(
        const std::vector<std::size_t> &costs,
        const MergeStrategy &merge_strategy = default_merge_strategy) {
        // Priority queue comparison
        using PQElem = std::unique_ptr<QueueElem>;
        struct Compare {
            const std::function<bool(const QueueElem *, const QueueElem *)> &ms;

            Compare(const std::function<bool(const QueueElem *, const QueueElem *)> &ms) : ms(ms) {
            }

            bool operator()(const PQElem &a, const PQElem &b) const {
                return ms(a.get(), b.get());
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

    /// Compute optimal merge schedule (based on domain size cost)
    /// @param domain_sizes: domain size for each variable, already sorted
    /// @param merge_strategy Function to determine merge order (default: default_merge_strategy).
    /// @returns MergeSchedule: plan to combine nodes, canonical variable order, bitvector
    inline MergeSchedule compute_merge_schedule(const std::vector<std::size_t> &domain_sizes,
        const MergeStrategy &merge_strategy = default_merge_strategy) {
        auto merge_tree = compute_merge_tree(domain_sizes, merge_strategy);
        const auto &[variable_order_, traversal_bits_] = recursive_tree_dfs(merge_tree.get());


        dynamic_bitset::DynamicBitset<> traversal(traversal_bits_.size());
        for (auto i = 0; i < traversal_bits_.size(); ++i) {
            if (traversal_bits_[i])
                traversal.set(i);
        }

        auto mid_spans = precompute_all_calc_mids(traversal);
        utils::g_log << "MergeSchedule: traversal precomputed with " << mid_spans.size() << " splits." << std::endl;
        return MergeSchedule{std::move(variable_order_), std::move(traversal), std::move(mid_spans)};
    }
}

#endif //SUBTREE_SPLIT_INDEX_H

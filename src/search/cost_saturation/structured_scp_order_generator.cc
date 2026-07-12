#include "structured_scp_order_generator.h"

#include "abstraction.h"
#include "abstraction_generator.h"
#include "types.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <tuple>
#include <vector>

using namespace std;

namespace cost_saturation {
// Mix an (operator, cost) pair into a 64-bit value (splitmix64 finalizer).
static uint64_t mix_op_cost(int op_id, int cost) {
    uint64_t x = static_cast<uint64_t>(op_id) * 0x9E3779B97F4A7C15ULL ^
        static_cast<uint64_t>(static_cast<unsigned int>(cost));
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static const int BITS_PER_WORD = 64;

static int get_num_mask_words(int num_bits) {
    return (num_bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

static void set_mask_bit(OpMask &mask, int i) {
    mask[i / BITS_PER_WORD] |= uint64_t(1) << (i % BITS_PER_WORD);
}

static bool test_mask_bit(const OpMask &mask, int i) {
    return (mask[i / BITS_PER_WORD] >> (i % BITS_PER_WORD)) & 1;
}

StructuredSCPOrderGenerator::StructuredSCPOrderGenerator(
    const shared_ptr<AbstractTask> &transform, Abstractions abstractions,
    const StructuredSCPOptions &options, utils::Verbosity verbosity)
    : abstractions(move(abstractions)),
      task_proxy(*transform),
      options(options),
      precomputed_conflicting_ops(false),
      recomputed_lookup_tables(0),
      lookup_cache_hits(0),
      log(utils::get_log_for_verbosity(verbosity)) {
    int num_abstractions = this->abstractions.size();
    lookup_tables.resize(num_abstractions);
    lookup_sscp_node_cache.resize(num_abstractions);
    scf_cache.resize(num_abstractions);
    table_by_restricted_costs.resize(num_abstractions);
    unsolvability_infos.reserve(num_abstractions);
    for (int abstraction_id = 0; abstraction_id < num_abstractions;
         ++abstraction_id) {
        unsolvability_infos.emplace_back(
            abstraction_id,
            this->abstractions[abstraction_id]->get_num_states());
    }
    cout << "Number of abstractions: " << num_abstractions << endl;
    if (options.max_lookup_table_cache_resizes != -1) {
        // Historically, hash map resizes happened at 87.5% load.
        max_lookup_table_entries = static_cast<CostKey>(
            0.875 * pow(2, options.max_lookup_table_cache_resizes));
    } else {
        max_lookup_table_entries = numeric_limits<CostKey>::max();
    }
    cout << "Max lookup table entries: " << max_lookup_table_entries << endl;
}

void StructuredSCPOrderGenerator::precompute_operator_properties(
    const vector<int> &relevant_abstraction_ids) {
    vector<bool> abstraction_is_relevant(abstractions.size(), false);
    for (int abstraction_id : relevant_abstraction_ids) {
        abstraction_is_relevant[abstraction_id] = true;
    }
    if (options.use_affecting_labels || options.use_non_negative_labels) {
        precompute_relevant_ops(abstraction_is_relevant);
    }
    precompute_ops_with_nonincreasing_remaining_cost(abstraction_is_relevant);
    cout << "Relevant abstractions: " << relevant_abstraction_ids.size()
         << endl;
    if (options.use_affecting_labels &&
        relevant_abstraction_ids.size() <= 2000) {
        precompute_conflicting_ops(abstraction_is_relevant);
        log << "Precomputed conflicting ops" << endl;
    } else {
        int num_words = get_num_mask_words(task_proxy.get_operators().size());
        conflicting_ops = {{OpMask(num_words, 0)}};
    }
}

void StructuredSCPOrderGenerator::precompute_relevant_ops(
    const vector<bool> &abstraction_is_relevant) {
    int num_operators = task_proxy.get_operators().size();
    int num_words = get_num_mask_words(num_operators);
    relevant_ops_by_abstraction.reserve(abstractions.size());
    inf_donating_ops_by_abstraction.reserve(abstractions.size());
    int abstraction_id = 0;
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        OpMask relevant_ops(num_words, 0);
        OpMask inf_donating_ops(num_words, 0);
        if (abstraction_is_relevant[abstraction_id]) {
            for (int op_id = 0; op_id < num_operators; ++op_id) {
                if (abstraction->operator_is_scp_active(op_id)) {
                    assert(abstraction->operator_is_active(op_id));
                    set_mask_bit(relevant_ops, op_id);
                } else if (options.use_general_cp &&
                           !abstraction->operator_is_active(op_id) &&
                           !abstraction->operator_induces_self_loop(op_id)) {
                    /* With general cost partitioning, an operator without
                       any transition has saturated cost -infinity (an empty
                       supremum), so saturating this abstraction leaves
                       infinite remaining cost for the operator to all later
                       abstractions. If the operator affects a later
                       abstraction, its heuristic values can increase, so
                       the order of the two abstractions matters even though
                       their saturation-affecting labels are disjoint.
                       Abstractions that prune transitions (e.g., Cartesian
                       abstractions drop transitions of unsolvable states)
                       can lack an operator that still affects other
                       abstractions; for projections this never happens. */
                    set_mask_bit(inf_donating_ops, op_id);
                }
            }
        }
        relevant_ops_by_abstraction.push_back(move(relevant_ops));
        inf_donating_ops_by_abstraction.push_back(move(inf_donating_ops));
        ++abstraction_id;
    }

    relevant_op_ids_by_abstraction.resize(abstractions.size());
    for (size_t id = 0; id < abstractions.size(); ++id) {
        for (int op_id = 0; op_id < num_operators; ++op_id) {
            if (test_mask_bit(relevant_ops_by_abstraction[id], op_id)) {
                relevant_op_ids_by_abstraction[id].push_back(op_id);
            }
        }
    }
}

/* The order of two abstractions can only influence their heuristic values
   if some operator affects both of them (like for additive pattern
   databases), or if one abstraction donates infinite remaining cost for an
   operator that affects the other. */
void StructuredSCPOrderGenerator::compute_conflicting_ops(
    int id1, int id2, OpMask &conflict) const {
    const OpMask &relevant1 = relevant_ops_by_abstraction[id1];
    const OpMask &relevant2 = relevant_ops_by_abstraction[id2];
    const OpMask &donating1 = inf_donating_ops_by_abstraction[id1];
    const OpMask &donating2 = inf_donating_ops_by_abstraction[id2];
    for (size_t w = 0; w < conflict.size(); ++w) {
        conflict[w] = (relevant1[w] & relevant2[w]) |
            (donating1[w] & relevant2[w]) | (donating2[w] & relevant1[w]);
    }
}

void StructuredSCPOrderGenerator::
precompute_ops_with_nonincreasing_remaining_cost(
    const vector<bool> &abstraction_is_relevant) {
    int num_operators = task_proxy.get_operators().size();
    int num_words = get_num_mask_words(num_operators);
    // By default, all operators are considered nonincreasing.
    op_has_nonincreasing_remaining_costs = vector<OpMask>(
        abstractions.size(), OpMask(num_words, ~uint64_t(0)));

    // With non-negative cost partitioning, all operators are nonincreasing.
    if (options.use_general_cp && options.use_non_negative_labels) {
        for (size_t abstraction_id = 0; abstraction_id < abstractions.size();
             ++abstraction_id) {
            if (abstraction_is_relevant[abstraction_id]) {
                vector<bool> non_increasing_ops =
                    abstractions[abstraction_id]
                    ->get_operators_with_non_increasing_remaining_cost();
                OpMask &mask =
                    op_has_nonincreasing_remaining_costs[abstraction_id];
                const OpMask &relevant_ops =
                    relevant_ops_by_abstraction[abstraction_id];
                for (int op_id = 0; op_id < num_operators; ++op_id) {
                    if (test_mask_bit(relevant_ops, op_id) &&
                        !non_increasing_ops[op_id]) {
                        mask[op_id / BITS_PER_WORD] &=
                            ~(uint64_t(1) << (op_id % BITS_PER_WORD));
                    }
                }
            }
        }
    }
}

void StructuredSCPOrderGenerator::precompute_conflicting_ops(
    const vector<bool> &abstraction_is_relevant) {
    precomputed_conflicting_ops = true;
    int num_words = get_num_mask_words(task_proxy.get_operators().size());
    conflicting_ops = vector<vector<OpMask>>(
        abstractions.size(),
        vector<OpMask>(abstractions.size(), OpMask(num_words, 0)));
    statically_conflicting_pairs = vector<OpMask>(
        abstractions.size(),
        OpMask(get_num_mask_words(abstractions.size()), 0));
    for (size_t id1 = 0; id1 < abstractions.size(); ++id1) {
        if (abstraction_is_relevant[id1]) {
            for (size_t id2 = id1 + 1; id2 < abstractions.size(); ++id2) {
                if (abstraction_is_relevant[id2]) {
                    OpMask &conflict = conflicting_ops[id1][id2];
                    compute_conflicting_ops(id1, id2, conflict);
                    if (any_of(conflict.begin(), conflict.end(),
                               [](uint64_t word) {return word != 0;})) {
                        set_mask_bit(statically_conflicting_pairs[id1], id2);
                        set_mask_bit(statically_conflicting_pairs[id2], id1);
                    }
                }
            }
        }
    }
    // When conflicting ops are precomputed, the relevant ops are not needed
    // anymore.
    utils::release_vector_memory(relevant_ops_by_abstraction);
    utils::release_vector_memory(inf_donating_ops_by_abstraction);
}

bool StructuredSCPOrderGenerator::is_non_trivial_sum_node(
    NodeId node) const {
    return nodes[node].type == NodeType::SUM &&
           any_of(nodes.children_begin(node), nodes.children_end(node),
                  [&](NodeId child) {
                      return nodes[child].type != NodeType::LOOKUP;
                  });
}

StructuredSCPOrder StructuredSCPOrderGenerator::generate() {
    utils::Timer timer;
    NodeId root_node = create_sscp_order_dag();
    assert(abstractions.size() == unsolvability_infos.size());
    assert(lookup_tables.size() == abstractions.size());
    assert(lookup_sscp_node_cache.size() == abstractions.size());
    cout << "Recomputed lookup tables: " << recomputed_lookup_tables << endl;
    cout << "Lookup table cache hits: " << lookup_cache_hits << endl;
    cout << "Lookup table cache size: "
         << lookup_tables_cache.size() * abstractions.size() << endl;
    cout << "Stored cost functions: " << packed_costs.size() << endl;
    int num_lookup_nodes = 0;
    int num_sum_nodes = 0;
    int num_nontrivial_sum_nodes = 0;
    int num_max_nodes = 0;
    for (NodeId node = 0; node < static_cast<int>(nodes.size()); ++node) {
        switch (nodes[node].type) {
        case NodeType::LOOKUP:
            ++num_lookup_nodes;
            break;
        case NodeType::MAX:
            ++num_max_nodes;
            break;
        case NodeType::SUM:
            ++num_sum_nodes;
            if (is_non_trivial_sum_node(node)) {
                ++num_nontrivial_sum_nodes;
            }
            break;
        }
    }
    log << "Time to generate DAG: " << timer() << endl;
    log << "Generated nodes: " << nodes.size() << endl;
    log << "Generated sum nodes: " << num_sum_nodes << endl;
    log << "Generated nontrivial sum nodes: " << num_nontrivial_sum_nodes
        << endl;
    log << "Generated max nodes: " << num_max_nodes << endl;
    log << "Generated lookup table entries: " << num_lookup_nodes << endl;
    return create_structured_scp_order(root_node);
}

// Sentinel table id caching that the lookup node was pruned for these costs.
static const int PRUNED_LOOKUP = -1;
// Sentinel for lookup nodes that have not been computed yet.
static const int UNKNOWN_LOOKUP = -2;

NodeId StructuredSCPOrderGenerator::create_lookup_node(
    const Costs &costs, CostKey cost_key, int abstraction_id) {
    bool cache_this_lookup =
        options.cache_lookup_tables && cost_key < max_lookup_table_entries;
    if (cache_this_lookup) {
        if (cost_key >= lookup_tables_cache.size()) {
            lookup_tables_cache.resize(cost_key + 1);
        }
        vector<int> &row = lookup_tables_cache[cost_key];
        if (row.empty()) {
            row.assign(abstractions.size(), UNKNOWN_LOOKUP);
        }
        int cached_table_id = row[abstraction_id];
        if (cached_table_id != UNKNOWN_LOOKUP) {
            ++lookup_cache_hits;
            if (cached_table_id == PRUNED_LOOKUP) {
                return NO_NODE;
            }
            assert(utils::in_bounds(
                       cached_table_id,
                       lookup_sscp_node_cache[abstraction_id]));
            return lookup_sscp_node_cache[abstraction_id][cached_table_id];
        }
    }
    auto cache_table_for_costs = [&](int table_id) {
            if (cache_this_lookup) {
                lookup_tables_cache[cost_key][abstraction_id] = table_id;
            }
        };

    /* The goal distances only depend on the costs of the abstraction's
       relevant operators, so cost functions that agree on them share the
       lookup table and we can skip the goal distance computation. */
    gtl::flat_hash_map<Costs, int, VectorIntMurmurHash>::iterator
        restricted_it;
    bool restrict_costs = !relevant_op_ids_by_abstraction.empty();
    if (restrict_costs) {
        restricted_costs_scratch.clear();
        for (int op_id : relevant_op_ids_by_abstraction[abstraction_id]) {
            restricted_costs_scratch.push_back(costs[op_id]);
        }
        bool inserted;
        tie(restricted_it, inserted) =
            table_by_restricted_costs[abstraction_id].try_emplace(
                restricted_costs_scratch, UNKNOWN_LOOKUP);
        if (!inserted) {
            int table_id = restricted_it->second;
            cache_table_for_costs(table_id);
            if (table_id == PRUNED_LOOKUP) {
                return NO_NODE;
            }
            return lookup_sscp_node_cache[abstraction_id][table_id];
        }
    }
    auto cache_table_for_restricted_costs = [&](int table_id) {
            if (restrict_costs) {
                restricted_it->second = table_id;
            }
            cache_table_for_costs(table_id);
        };

    vector<int> goal_distances =
        abstractions[abstraction_id]->compute_goal_distances(costs);
    // Prune this abstraction if all goal distances are 0.
    if (all_of(goal_distances.begin(), goal_distances.end(),
               [](int h) {return h == 0;})) {
        cache_table_for_restricted_costs(PRUNED_LOOKUP);
        return NO_NODE;
    }

    /* An abstraction with only 0 and INF values can be pruned from the DAG
       under non-general costs since it only contributes unsolvability
       information. */
    bool dead_end_detection_only =
        !options.use_general_cp &&
        all_of(goal_distances.begin(), goal_distances.end(),
               [](int h) {return h == 0 || h == INF;});

    int lookup_table_id = 0;
    for (; lookup_table_id <
         static_cast<int>(lookup_tables[abstraction_id].size());
         ++lookup_table_id) {
        const vector<int> &lookup_table =
            lookup_tables[abstraction_id][lookup_table_id];
        if (goal_distances == lookup_table) {
            ++recomputed_lookup_tables;
            if (dead_end_detection_only) {
                cache_table_for_restricted_costs(PRUNED_LOOKUP);
                return NO_NODE;
            }
            cache_table_for_restricted_costs(lookup_table_id);
            return lookup_sscp_node_cache[abstraction_id][lookup_table_id];
        }
    }
    lookup_tables[abstraction_id].push_back(move(goal_distances));
    NodeId node = nodes.add_lookup_node(abstraction_id, lookup_table_id);

    lookup_sscp_node_cache[abstraction_id].push_back(node);
    if (options.cache_scf_functions) {
        scf_cache[abstraction_id].emplace_back(
            compute_scf(
                *abstractions[abstraction_id], get_lookup_table(node),
                options.use_general_cp));
        assert(scf_cache[abstraction_id].size() ==
               lookup_tables[abstraction_id].size());
    }
    if (dead_end_detection_only) {
        cache_table_for_restricted_costs(PRUNED_LOOKUP);
        return NO_NODE;
    }
    cache_table_for_restricted_costs(lookup_table_id);
    return node;
}

CostContext StructuredSCPOrderGenerator::make_cost_context(
    Costs &&costs) {
    CostContext context{move(costs), 0, 0, {}, {}};
    for (size_t op_id = 0; op_id < context.costs.size(); ++op_id) {
        context.cost_hash += mix_op_cost(op_id, context.costs[op_id]);
    }
    context.key = lookup_costs_or_register(context.costs, context.cost_hash);
    if (options.use_affecting_labels) {
        int num_operators = context.costs.size();
        context.live_ops.assign(get_num_mask_words(num_operators), 0);
        context.cond_ops.assign(get_num_mask_words(num_operators), 0);
        vector<int> all_ops(num_operators);
        iota(all_ops.begin(), all_ops.end(), 0);
        update_cost_context(context, all_ops);
    }
    return context;
}

void StructuredSCPOrderGenerator::update_cost_context(
    CostContext &context, const vector<int> &changed_ops) const {
    if (!options.use_affecting_labels) {
        return;
    }
    for (int op_id : changed_ops) {
        uint64_t bit = uint64_t(1) << (op_id % BITS_PER_WORD);
        uint64_t &live_word = context.live_ops[op_id / BITS_PER_WORD];
        uint64_t &cond_word = context.cond_ops[op_id / BITS_PER_WORD];
        live_word &= ~bit;
        cond_word &= ~bit;
        if (options.use_infinite_labels && context.costs[op_id] == INF) {
            continue;
        }
        if (options.use_non_negative_labels && context.costs[op_id] == 0) {
            cond_word |= bit;
        } else {
            live_word |= bit;
        }
    }
}

vector<vector<int>>
StructuredSCPOrderGenerator::compute_independent_abstractions(
    const vector<int> &pending_abstraction_ids, const CostContext &context) {
    // Build the dependency graph between the abstractions.
    ccp::DisjointSet dependency_graph(abstractions.size());
    size_t num_pending = pending_abstraction_ids.size();
    for (size_t i = 0; i < num_pending; ++i) {
        int i_parent = dependency_graph.find(pending_abstraction_ids[i]);
        for (size_t j = i + 1; j < num_pending; ++j) {
            if (precomputed_conflicting_ops &&
                !test_mask_bit(
                    statically_conflicting_pairs[pending_abstraction_ids[i]],
                    pending_abstraction_ids[j])) {
                continue;
            }
            if (i_parent !=
                dependency_graph.find(pending_abstraction_ids[j])) {
                check_and_add_dependency(
                    dependency_graph, pending_abstraction_ids[i],
                    pending_abstraction_ids[j], context);
                /* Only pending abstractions are ever united, so a set of
                   size num_pending must contain all of them and no further
                   checks can change the components. */
                if (dependency_graph.get_set_size(
                        pending_abstraction_ids[i]) == num_pending) {
                    goto endloop;
                }
                i_parent = dependency_graph.find(pending_abstraction_ids[i]);
            }
        }
    }
 endloop:

    // Compute the connected components of the dependency graph.
    vector<vector<int>> independent_abstractions =
        dependency_graph.get_connected_components(pending_abstraction_ids);

    for (vector<int> &component : independent_abstractions) {
        sort(component.begin(), component.end());
    }
    sort(independent_abstractions.begin(), independent_abstractions.end());
    return independent_abstractions;
}

void StructuredSCPOrderGenerator::check_and_add_dependency(
    ccp::DisjointSet &dependency_graph, int id1, int id2,
    const CostContext &context) {
    assert(id1 < id2);

    if (options.use_affecting_labels) {
        const OpMask *conflicting_ops_of_abstractions = nullptr;
        if (precomputed_conflicting_ops) {
            conflicting_ops_of_abstractions = &conflicting_ops[id1][id2];
        } else {
            OpMask &scratch = conflicting_ops[0][0];
            compute_conflicting_ops(id1, id2, scratch);
            conflicting_ops_of_abstractions = &scratch;
        }
        const OpMask &conflict = *conflicting_ops_of_abstractions;
        const OpMask &nonincreasing1 =
            op_has_nonincreasing_remaining_costs[id1];
        const OpMask &nonincreasing2 =
            op_has_nonincreasing_remaining_costs[id2];
        for (size_t w = 0; w < conflict.size(); ++w) {
            /* An operator affecting both abstractions creates a dependency
               if it is live, or if it has remaining cost 0 and the
               abstractions do not both guarantee nonincreasing remaining
               costs. */
            uint64_t dependency_ops =
                conflict[w] &
                (context.live_ops[w] |
                 (context.cond_ops[w] &
                  ~(nonincreasing1[w] & nonincreasing2[w])));
            if (dependency_ops) {
                dependency_graph.unite(id1, id2);
                return;
            }
        }
    } else {
        int num_operators = task_proxy.get_operators().size();
        for (int op_id = 0; op_id < num_operators; ++op_id) {
            if (options.use_infinite_labels &&
                context.costs[op_id] == INF) {
                continue;
            }
            if (options.use_non_negative_labels &&
                context.costs[op_id] == 0 &&
                test_mask_bit(
                    op_has_nonincreasing_remaining_costs[id1], op_id) &&
                test_mask_bit(
                    op_has_nonincreasing_remaining_costs[id2], op_id)) {
                continue;
            }
            dependency_graph.unite(id1, id2);
            break;
        }
    }
}

namespace {
void collect_compositional_nodes(
    const NodeArena &nodes, NodeId node, vector<bool> &marked,
    vector<NodeId> &reachable) {
    if (nodes[node].type != NodeType::LOOKUP && !marked[node]) {
        marked[node] = true;
        reachable.push_back(node);
        for (const NodeId *child = nodes.children_begin(node);
             child != nodes.children_end(node); ++child) {
            collect_compositional_nodes(nodes, *child, marked, reachable);
        }
    }
}
}

StructuredSCPOrder StructuredSCPOrderGenerator::create_structured_scp_order(
    NodeId root_node) {
    // Free the construction-time caches before materializing instructions.
    decltype(cost_key_by_hash)().swap(cost_key_by_hash);
    utils::release_vector_memory(cost_key_overflow);
    utils::release_vector_memory(packed_costs.data);
    utils::release_vector_memory(packed_costs.offsets);
    utils::release_vector_memory(lookup_tables_cache);
    utils::release_vector_memory(table_by_restricted_costs);
    utils::release_vector_memory(relevant_op_ids_by_abstraction);
    utils::release_vector_memory(scf_cache);
    utils::release_vector_memory(conflicting_ops);
    utils::release_vector_memory(op_has_nonincreasing_remaining_costs);

    // Determine reachable compositional nodes and order them by level.
    vector<NodeId> reachable_compositional_nodes;
    vector<bool> marked(nodes.size(), false);
    if (root_node != NO_NODE) {
        collect_compositional_nodes(
            nodes, root_node, marked, reachable_compositional_nodes);
        /* Children are always created before their parents, so sorting by
           node id yields a topological order. */
        sort(reachable_compositional_nodes.begin(),
             reachable_compositional_nodes.end());
    }
    utils::release_vector_memory(marked);

    // Create compact lookup tables and extract unsolvability information.
    create_compact_lookup_tables();
    int useful_unsolvability_infos = 0;
    int num_unsolvable_states = 0;
    for (const UnsolvabilityInfo &info : unsolvability_infos) {
        if (info.useful) {
            ++useful_unsolvability_infos;
            num_unsolvable_states += count(
                info.unsolvable_states.begin(), info.unsolvable_states.end(),
                true);
        }
    }
    cout << "Useful unsolvability infos: " << useful_unsolvability_infos
         << endl;
    cout << "Unsolvable abstract states: " << num_unsolvable_states << endl;
    assert(abstractions.size() == unsolvability_infos.size());

    /* Assign each node its value id in the heuristic evaluation: lookup
       nodes get the positions of their table entries, compositional nodes
       follow in level order. */
    vector<int> value_ids(nodes.size(), -1);
    int value_id = 0;
    int num_lookup_nodes = 0;
    for (const vector<NodeId> &lookup_nodes_by_abstraction :
         lookup_sscp_node_cache) {
        for (NodeId node : lookup_nodes_by_abstraction) {
            value_ids[node] = value_id;
            ++value_id;
            ++num_lookup_nodes;
        }
    }

    // Create the instructions for the compositional nodes.
    int64_t total_ids = 0;
    for (NodeId node : reachable_compositional_nodes) {
        total_ids += nodes[node].num_children;
    }
    Instructions instructions;
    instructions.reserve(reachable_compositional_nodes.size(), total_ids);
    int num_reachable_nodes =
        static_cast<int>(reachable_compositional_nodes.size()) +
        num_lookup_nodes;
    int num_reachable_sum_nodes = 0;
    int num_reachable_non_trivial_sum_nodes = 0;
    int num_reachable_max_nodes = 0;
    for (NodeId node : reachable_compositional_nodes) {
        assert(nodes[node].num_children > 0);
        value_ids[node] = value_id;
        ++value_id;

        if (nodes[node].type == NodeType::MAX) {
            instructions.types.push_back(InstructionType::MAX);
            ++num_reachable_max_nodes;
        } else {
            assert(nodes[node].type == NodeType::SUM);
            instructions.types.push_back(InstructionType::SUM);
            ++num_reachable_sum_nodes;
            if (is_non_trivial_sum_node(node)) {
                ++num_reachable_non_trivial_sum_nodes;
            }
        }
        for (const NodeId *child = nodes.children_begin(node);
             child != nodes.children_end(node); ++child) {
            assert(value_ids[*child] != -1);
            assert(value_ids[*child] < value_ids[node]);
            instructions.ids.push_back(value_ids[*child]);
        }
        instructions.id_offsets.push_back(instructions.ids.size());
    }
    AbstractionFunctions abs_functions;
    abs_functions.reserve(abstractions.size());
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        abs_functions.push_back(abstraction->extract_abstraction_function());
    }
    cout << "Reachable nodes: " << num_reachable_nodes << endl;
    cout << "Reachable sum nodes: " << num_reachable_sum_nodes << endl;
    cout << "Reachable nontrivial sum nodes: "
         << num_reachable_non_trivial_sum_nodes << endl;
    cout << "Reachable max nodes: " << num_reachable_max_nodes << endl;
    cout << "Used abstractions: " << abstractions.size() << endl;
    assert(abstractions.size() == unsolvability_infos.size());
    return StructuredSCPOrder{
        move(abs_functions), move(unsolvability_infos), move(instructions),
        move(lookup_tables)};
}

void StructuredSCPOrderGenerator::create_compact_lookup_tables() {
    vector<vector<vector<int>>> compact_lookup_tables;
    compact_lookup_tables.reserve(abstractions.size());
    assert(lookup_tables.size() == abstractions.size());
    int removed_abstractions = 0;
    for (size_t abstr_id = 0; abstr_id < abstractions.size(); ++abstr_id) {
        vector<vector<int>> &lookup_table_by_abstraction =
            lookup_tables[abstr_id];
        if (options.use_unsolvability_infos) {
            for (const vector<int> &lookup : lookup_table_by_abstraction) {
                for (size_t state_id = 0; state_id < lookup.size();
                     ++state_id) {
                    if (lookup[state_id] == INF) {
                        unsolvability_infos[abstr_id]
                        .unsolvable_states[state_id] = true;
                        unsolvability_infos[abstr_id].useful = true;
                    }
                }
            }
        }
        if (lookup_table_by_abstraction.empty() &&
            !unsolvability_infos[abstr_id].useful) {
            // Remove abstraction without lookup table.
            abstractions.erase(abstractions.begin() + abstr_id);
            lookup_tables.erase(lookup_tables.begin() + abstr_id);
            lookup_sscp_node_cache.erase(
                lookup_sscp_node_cache.begin() + abstr_id);
            unsolvability_infos.erase(
                unsolvability_infos.begin() + abstr_id);
            ++removed_abstractions;
            --abstr_id;
            continue;
        }
        /* Transpose the lookup tables of this abstraction so that all values
           for one abstract state are stored contiguously. */
        vector<vector<int>> compact_lookup_table_by_abstraction;
        if (!lookup_table_by_abstraction.empty()) {
            compact_lookup_table_by_abstraction.resize(
                lookup_table_by_abstraction[0].size());
            for (const vector<int> &lookup_table :
                 lookup_table_by_abstraction) {
                for (size_t i = 0; i < lookup_table.size(); ++i) {
                    compact_lookup_table_by_abstraction[i].push_back(
                        lookup_table[i]);
                }
            }
        }
        compact_lookup_tables.push_back(
            move(compact_lookup_table_by_abstraction));
    }
    cout << "Removed " << removed_abstractions << " abstractions." << endl;
    lookup_tables = move(compact_lookup_tables);
}

const SaturatedCostFunction &StructuredSCPOrderGenerator::get_saturated_costs(
    NodeId node) const {
    assert(nodes[node].type == NodeType::LOOKUP);
    if (options.cache_scf_functions) {
        return scf_cache[nodes[node].abstraction_id]
               [nodes[node].lookup_table_id];
    } else {
        scf_scratch = make_unique<SaturatedCostFunction>(
            compute_scf(
                *abstractions[nodes[node].abstraction_id],
                get_lookup_table(node), options.use_general_cp));
        return *scf_scratch;
    }
}

// Map INF to 0 and finite costs to cost + 1 to avoid a special case.
static unsigned int packed_cost_value(int cost) {
    return (cost == INF) ? 0 : static_cast<unsigned int>(cost) + 1;
}

/* Pack with a compile-time width that divides 8, so the loops have fixed
   stride and vectorize. */
template<int BITS>
static void pack_values(const Costs &costs, vector<uint8_t> &blob) {
    constexpr int PER_BYTE = 8 / BITS;
    size_t num_full_bytes = costs.size() / PER_BYTE;
    size_t offset = blob.size();
    blob.resize(offset + (costs.size() + PER_BYTE - 1) / PER_BYTE, 0);
    uint8_t *bytes = blob.data() + offset;
    for (size_t b = 0; b < num_full_bytes; ++b) {
        uint8_t value = 0;
        for (int j = 0; j < PER_BYTE; ++j) {
            value |= packed_cost_value(costs[b * PER_BYTE + j]) << (j * BITS);
        }
        bytes[b] = value;
    }
    for (size_t i = num_full_bytes * PER_BYTE; i < costs.size(); ++i) {
        bytes[num_full_bytes] |=
            packed_cost_value(costs[i]) << (i % PER_BYTE * BITS);
    }
}

template<typename UInt>
static void pack_values_wide(const Costs &costs, vector<uint8_t> &blob) {
    size_t offset = blob.size();
    blob.resize(offset + costs.size() * sizeof(UInt));
    UInt *values = reinterpret_cast<UInt *>(blob.data() + offset);
    for (size_t i = 0; i < costs.size(); ++i) {
        values[i] = static_cast<UInt>(packed_cost_value(costs[i]));
    }
}

void PackedCostsPool::pack(const Costs &costs, vector<uint8_t> &blob) {
    unsigned int max_finite_cost = 0;
    for (int cost : costs) {
        assert(cost >= 0);
        max_finite_cost = max(
            max_finite_cost,
            (cost == INF) ? 0 : static_cast<unsigned int>(cost));
    }
    uint8_t bits_per_cost = bit_ceil(
        static_cast<uint8_t>(bit_width(max_finite_cost + 1)));

    blob.clear();
    blob.reserve(1 + (bits_per_cost * costs.size() + 7) / 8);
    blob.push_back(bits_per_cost);
    switch (bits_per_cost) {
    case 1:
        pack_values<1>(costs, blob);
        break;
    case 2:
        pack_values<2>(costs, blob);
        break;
    case 4:
        pack_values<4>(costs, blob);
        break;
    case 8:
        pack_values_wide<uint8_t>(costs, blob);
        break;
    case 16:
        pack_values_wide<uint16_t>(costs, blob);
        break;
    default:
        pack_values_wide<uint32_t>(costs, blob);
        break;
    }
}

void StructuredSCPOrderGenerator::reduce_cost_context(
    CostContext &context, const SaturatedCostFunction &scf) {
    for (int op_id : scf.nonzero_ops) {
        int remaining = context.costs[op_id];
        int saturated = scf.costs[op_id];
        assert(remaining >= 0);
        assert(saturated <= remaining);
        assert(remaining == INF || saturated != INF);
        // Left addition: x - y = x for all values y if x is infinite.
        if (remaining != INF) {
            int reduced = (saturated == -INF) ? INF : remaining - saturated;
            assert(reduced >= 0);
            context.costs[op_id] = reduced;
            context.cost_hash += mix_op_cost(op_id, reduced) -
                mix_op_cost(op_id, remaining);
        }
    }
    update_cost_context(context, scf.nonzero_ops);
    context.key = lookup_costs_or_register(context.costs, context.cost_hash);
}

/* Return the key under which the given cost function is registered, so
   that all other hash maps can use the small key instead of the full cost
   function. */
CostKey StructuredSCPOrderGenerator::lookup_costs_or_register(
    const Costs &costs, uint64_t hash) {
    auto it = cost_key_by_hash.find(hash);
    if (it == cost_key_by_hash.end()) {
        CostKey key = packed_costs.size();
        PackedCostsPool::pack(costs, packed_costs_scratch);
        packed_costs.append(packed_costs_scratch);
        cost_key_by_hash.emplace(hash, key);
        return key;
    }
    PackedCostsPool::pack(costs, packed_costs_scratch);
    if (packed_costs.equals(it->second, packed_costs_scratch)) {
        return it->second;
    }
    // Hash collision: look for the cost function in the overflow list.
    for (const auto &[overflow_hash, overflow_key] : cost_key_overflow) {
        if (overflow_hash == hash &&
            packed_costs.equals(overflow_key, packed_costs_scratch)) {
            return overflow_key;
        }
    }
    CostKey key = packed_costs.size();
    packed_costs.append(packed_costs_scratch);
    cost_key_overflow.emplace_back(hash, key);
    return key;
}

void add_structured_order_generator_options_to_parser(
    plugins::Feature &feature) {
    feature.add_option<shared_ptr<AbstractTask>>(
        "transform",
        "Optional task transformation for the heuristic."
        " Currently, adapt_costs() and no_transform() are available.",
        "no_transform()");
    feature.add_list_option<shared_ptr<AbstractionGenerator>>(
        "abstraction_generators",
        "available generators are cartesian() and projections()",
        "[projections(hillclimbing(max_time=60, random_seed=0)),"
        " projections(systematic(2)), cartesian()]");
    feature.add_option<bool>(
        "use_unsolvability_infos",
        "extract unsolvability information to one table per abstraction",
        "true");
    feature.add_option<bool>(
        "use_general_cp", "use general cost partitioning", "true");
    feature.add_option<bool>(
        "use_affecting_labels",
        "disregard disjunct affecting labels for order dependence", "true");
    feature.add_option<bool>(
        "use_non_negative_labels",
        "disregard zero cost labels if they are not negative", "true");
    feature.add_option<bool>(
        "use_infinite_labels", "disregard infinite cost labels", "true");
    feature.add_option<bool>(
        "use_cost_partitioning_check", "post check for cost partitioning",
        "true");
    feature.add_option<bool>(
        "cache_scf_functions", "cache saturated cost functions", "true");
    feature.add_option<int>(
        "max_lookup_table_cache_resizes",
        "maximum number of lookup table cache resizes", "-1");
    feature.add_option<bool>(
        "cache_lookup_tables",
        "cache lookup table ids by cost function to avoid recomputing "
        "goal distances", "true");
    utils::add_log_options_to_feature(feature);
}

tuple<shared_ptr<AbstractTask>, Abstractions, StructuredSCPOptions,
      utils::Verbosity>
get_structured_scp_order_generator_arguments_from_options(
    const plugins::Options &opts) {
    StructuredSCPOptions options{
        opts.get<bool>("use_unsolvability_infos"),
        opts.get<bool>("use_general_cp"),
        opts.get<bool>("cache_lookup_tables"),
        opts.get<bool>("use_affecting_labels"),
        opts.get<bool>("use_non_negative_labels"),
        opts.get<bool>("use_infinite_labels"),
        opts.get<bool>("use_cost_partitioning_check"),
        opts.get<bool>("cache_scf_functions"),
        opts.get<int>("max_lookup_table_cache_resizes"),
    };
    if (options.use_cost_partitioning_check && !options.use_infinite_labels) {
        ABORT("use_cost_partitioning_check=true requires "
              "use_infinite_labels=true");
    }
    cout << "Generating abstractions..." << endl;
    Abstractions abstractions = generate_abstractions(
        opts.get<shared_ptr<AbstractTask>>("transform"),
        opts.get<vector<shared_ptr<AbstractionGenerator>>>(
            "abstraction_generators"));
    cout << "Generated " << abstractions.size() << " abstractions" << endl;
    return tuple_cat(
        forward_as_tuple(
            opts.get<shared_ptr<AbstractTask>>("transform"),
            move(abstractions), move(options)),
        utils::get_log_arguments_from_options(opts));
}

static class StructuredSCPOrderGeneratorCategoryPlugin
    : public plugins::TypedCategoryPlugin<StructuredSCPOrderGenerator> {
public:
    StructuredSCPOrderGeneratorCategoryPlugin()
        : TypedCategoryPlugin("StructuredSCPOrderGenerator") {
        document_synopsis("Generate structured SCP orders.");
    }
} _category_plugin;
}

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
#include <cmath>
#include <iostream>
#include <memory>
#include <tuple>
#include <vector>

using namespace std;

namespace cost_saturation {
int SSCPNode::num_compositional_nodes = 0;
int SSCPNode::num_lookup_nodes = 0;
int SumSSCPNode::num_sum_nodes = 0;
int SumSSCPNode::num_nontrivial_sum_nodes = 0;
int MaxSSCPNode::num_max_nodes = 0;

bool g_hacked_use_affecting_labels = true;
bool g_hacked_use_non_negative_labels = true;
bool g_hacked_use_infinite_labels = true;
bool g_hacked_use_cost_partitioning_check = true;
bool g_hacked_cache_scf_functions = true;
int g_hacked_max_lookup_table_cache_resizes = -1;

bool SumSSCPNode::is_non_trivial() const {
    return any_of(
        children.begin(), children.end(),
        [](const shared_ptr<SSCPNode> &child) {
            return !dynamic_pointer_cast<LookupSSCPNode>(child);
        });
}

StructuredSCPOrderGenerator::StructuredSCPOrderGenerator(
    const shared_ptr<AbstractTask> &transform, Abstractions abstractions,
    bool use_unsolvability_infos, bool use_general_cp,
    bool cache_lookup_tables, bool time_connected_components,
    utils::Verbosity verbosity)
    : abstractions(move(abstractions)),
      task_proxy(*transform),
      use_general_cp(use_general_cp),
      use_unsolvability(use_unsolvability_infos),
      cache_lookup_tables(cache_lookup_tables),
      time_connected_components(time_connected_components),
      precomputed_conflicting_ops(false),
      recomputed_lookup_tables(0),
      lookup_cache_hits(0),
      log(utils::get_log_for_verbosity(verbosity)),
      cc_generation(false),
      cc_computation(false) {
    int num_abstractions = this->abstractions.size();
    lookup_tables.resize(num_abstractions);
    lookup_sscp_node_cache.resize(num_abstractions);
    lookup_tables_cache.resize(num_abstractions);
    unsolvability_infos.reserve(num_abstractions);
    for (int abstraction_id = 0; abstraction_id < num_abstractions;
         ++abstraction_id) {
        unsolvability_infos.emplace_back(
            abstraction_id,
            this->abstractions[abstraction_id]->get_num_states());
    }
    cout << "Number of abstractions: " << num_abstractions << endl;
    if (g_hacked_max_lookup_table_cache_resizes != -1) {
        // Hash map resizes happen at 87.5% load.
        max_lookup_table_entries = floor(
            0.875 * pow(2, g_hacked_max_lookup_table_cache_resizes));
    } else {
        max_lookup_table_entries = numeric_limits<double>::infinity();
    }
    cout << "Max lookup table entries: " << max_lookup_table_entries << endl;
}

void StructuredSCPOrderGenerator::precompute_operator_properties(
    const vector<int> &relevant_abstraction_ids) {
    vector<bool> abstraction_is_relevant(abstractions.size(), false);
    for (int abstraction_id : relevant_abstraction_ids) {
        abstraction_is_relevant[abstraction_id] = true;
    }
    if (g_hacked_use_affecting_labels || g_hacked_use_non_negative_labels) {
        precompute_relevant_ops(abstraction_is_relevant);
    }
    if (g_hacked_use_non_negative_labels) {
        precompute_ops_with_nonincreasing_remaining_cost(
            abstraction_is_relevant);
    }
    cout << "Relevant abstractions: " << relevant_abstraction_ids.size()
         << endl;
    if (g_hacked_use_affecting_labels &&
        relevant_abstraction_ids.size() <= 2000) {
        precompute_conflicting_ops(abstraction_is_relevant);
        log << "Precomputed conflicting ops" << endl;
    } else {
        conflicting_ops = {{gtl::bit_vector(0)}};
    }
}

void StructuredSCPOrderGenerator::precompute_relevant_ops(
    const vector<bool> &abstraction_is_relevant) {
    int num_operators = task_proxy.get_operators().size();
    relevant_ops_by_abstraction.reserve(abstractions.size());
    int abstraction_id = 0;
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        gtl::bit_vector relevant_ops(num_operators);
        if (abstraction_is_relevant[abstraction_id]) {
            for (int op_id = 0; op_id < num_operators; ++op_id) {
                if (abstraction->operator_is_scp_active(op_id)) {
                    assert(abstraction->operator_is_active(op_id));
                    relevant_ops.set(op_id);
                }
            }
        }
        relevant_ops_by_abstraction.push_back(relevant_ops);
        ++abstraction_id;
    }
}

void StructuredSCPOrderGenerator::
precompute_ops_with_nonincreasing_remaining_cost(
    const vector<bool> &abstraction_is_relevant) {
    op_has_nonincreasing_remaining_costs = vector<vector<bool>>(
        abstractions.size(),
        vector<bool>(task_proxy.get_operators().size(), true));

    // With non-negative cost partitioning, all operators are nonincreasing.
    if (use_general_cp) {
        for (size_t abstraction_id = 0; abstraction_id < abstractions.size();
             ++abstraction_id) {
            if (abstraction_is_relevant[abstraction_id]) {
                vector<bool> non_increasing_ops =
                    abstractions[abstraction_id]
                    ->get_operators_with_non_increasing_remaining_cost();
                size_t op_id =
                    relevant_ops_by_abstraction[abstraction_id].find_first();
                while (op_id != gtl::bit_vector::npos) {
                    op_has_nonincreasing_remaining_costs[abstraction_id]
                    [op_id] = non_increasing_ops[op_id];
                    op_id = relevant_ops_by_abstraction[abstraction_id]
                        .find_next(op_id + 1);
                }
            }
        }
    }
}

void StructuredSCPOrderGenerator::precompute_conflicting_ops(
    const vector<bool> &abstraction_is_relevant) {
    precomputed_conflicting_ops = true;
    conflicting_ops = vector<vector<gtl::bit_vector>>(
        abstractions.size(),
        vector<gtl::bit_vector>(
            abstractions.size(),
            gtl::bit_vector(task_proxy.get_operators().size())));
    for (size_t id1 = 0; id1 < abstractions.size(); ++id1) {
        if (abstraction_is_relevant[id1]) {
            const gtl::bit_vector &relevant_ops1 =
                relevant_ops_by_abstraction[id1];
            for (size_t id2 = id1 + 1; id2 < abstractions.size(); ++id2) {
                if (abstraction_is_relevant[id2]) {
                    const gtl::bit_vector &relevant_ops2 =
                        relevant_ops_by_abstraction[id2];
                    conflicting_ops[id1][id2] = relevant_ops1 | relevant_ops2;
                }
            }
        }
    }
    // When conflicting ops are precomputed, the relevant ops are not needed
    // anymore.
    utils::release_vector_memory(relevant_ops_by_abstraction);
}

StructuredSCPOrder StructuredSCPOrderGenerator::generate() {
    utils::Timer timer;
    shared_ptr<SSCPNode> root_node = create_sscp_order_dag();
    assert(abstractions.size() == unsolvability_infos.size());
    assert(lookup_tables.size() == abstractions.size());
    assert(lookup_sscp_node_cache.size() == abstractions.size());
    cout << "Recomputed lookup tables: " << recomputed_lookup_tables << endl;
    cout << "Lookup table cache hits: " << lookup_cache_hits << endl;
    int lookup_table_cache_size = 0;
    for (const auto &lookup_table : lookup_tables_cache) {
        lookup_table_cache_size += lookup_table.size();
    }
    cout << "Lookup table cache size: " << lookup_table_cache_size << endl;
    cout << "SCF cache size: " << scf_cache.size() << endl;
    cout << "Stored cost functions: " << cost_key_cache.size() << endl;
    if (time_connected_components) {
        cout << "CC generation time: " << cc_generation << endl;
        cout << "CC computation time: " << cc_computation << endl;
    }
    log << "Time to generate DAG: " << timer() << endl;
    log << "Depth of DAG: " << (root_node ? root_node->level : 0) << endl;
    log << "Generated nodes: "
        << SSCPNode::num_compositional_nodes + SSCPNode::num_lookup_nodes
        << endl;
    log << "Generated sum nodes: " << SumSSCPNode::num_sum_nodes << endl;
    log << "Generated nontrivial sum nodes: "
        << SumSSCPNode::num_nontrivial_sum_nodes << endl;
    log << "Generated max nodes: " << MaxSSCPNode::num_max_nodes << endl;
    log << "Generated lookup table entries: " << SSCPNode::num_lookup_nodes
        << endl;
    return create_structured_scp_order(root_node);
}

void StructuredSCPOrderGenerator::dump_tree(
    const shared_ptr<SSCPNode> &node) const {
    cout << "---- dumping DAG ------" << endl;
    cout << "overall costs: ";
    print_indexed_vector(task_properties::get_operator_costs(task_proxy));
    if (node) {
        dump_node(node, nullptr);
    } else {
        cout << "empty DAG" << endl;
    }
}

void StructuredSCPOrderGenerator::dump_node(
    const shared_ptr<SSCPNode> &node,
    const shared_ptr<SSCPNode> &parent) const {
    cout << "node: " << node->index << " at level: " << node->level << endl;
    if (dynamic_pointer_cast<MaxSSCPNode>(node)) {
        cout << "max node with " << node->children.size() << " children"
             << endl;
    } else if (dynamic_pointer_cast<SumSSCPNode>(node)) {
        cout << "sum node with " << node->children.size() << " children"
             << endl;
    } else if (
        const auto lookup_node = dynamic_pointer_cast<LookupSSCPNode>(node)) {
        cout << "lookup node for abstraction: " << lookup_node->abstraction_id
             << endl;
    }
    if (parent) {
        cout << "parent: " << parent->index << endl;
    }
    for (const shared_ptr<SSCPNode> &child : node->children) {
        if (child) {
            dump_node(child, node);
        } else {
            cout << "null child" << endl;
        }
    }
}

shared_ptr<LookupSSCPNode> StructuredSCPOrderGenerator::create_lookup_node(
    const Costs &costs, CostKey cost_key, int abstraction_id) {
    if (cache_lookup_tables) {
        if (auto it = lookup_tables_cache[abstraction_id].find(cost_key);
            it != lookup_tables_cache[abstraction_id].end()) {
            ++lookup_cache_hits;
            assert(utils::in_bounds(abstraction_id, lookup_sscp_node_cache));
            assert(utils::in_bounds(
                       it->second, lookup_sscp_node_cache[abstraction_id]));
            return lookup_sscp_node_cache[abstraction_id][it->second];
        }
    }
    vector<int> goal_distances =
        abstractions[abstraction_id]->compute_goal_distances(costs);
    // Prune this abstraction if all goal distances are 0.
    if (all_of(goal_distances.begin(), goal_distances.end(),
               [](int h) {return h == 0;})) {
        return nullptr;
    }

    /* An abstraction with only 0 and INF values can be pruned from the DAG
       under non-general costs since it only contributes unsolvability
       information. */
    bool dead_end_detection_only =
        !use_general_cp &&
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
            if (cache_lookup_tables &&
                lookup_tables_cache[abstraction_id].size() <
                max_lookup_table_entries) {
                lookup_tables_cache[abstraction_id].insert(
                    {cost_key, lookup_table_id});
            }
            return lookup_sscp_node_cache[abstraction_id][lookup_table_id];
        }
    }
    lookup_tables[abstraction_id].push_back(move(goal_distances));
    auto node = make_shared<LookupSSCPNode>(abstraction_id, lookup_table_id);

    lookup_sscp_node_cache[abstraction_id].push_back(node);
    if (cache_lookup_tables &&
        lookup_tables_cache[abstraction_id].size() <
        max_lookup_table_entries) {
        lookup_tables_cache[abstraction_id].insert(
            {cost_key, lookup_table_id});
    }
    if (g_hacked_cache_scf_functions) {
        scf_cache.push_back(
            compute_scf(
                *abstractions[abstraction_id], get_lookup_table(node),
                use_general_cp));
        // Assert that the node index matches the cache position.
        assert(static_cast<int>(scf_cache.size()) == -node->index);
    }
    if (dead_end_detection_only) {
        return nullptr;
    }
    return node;
}

vector<vector<int>>
StructuredSCPOrderGenerator::compute_independent_abstractions(
    const vector<int> &pending_abstraction_ids, const Costs &remaining_costs) {
    // Build the dependency graph between the abstractions.
    if (time_connected_components) {
        cc_generation.resume();
    }
    ccp::DisjointSet dependency_graph(abstractions.size());
    size_t num_pending = pending_abstraction_ids.size();
    for (size_t i = 0; i < num_pending; ++i) {
        int i_parent = dependency_graph.find(pending_abstraction_ids[i]);
        for (size_t j = i + 1; j < num_pending; ++j) {
            if (i_parent !=
                dependency_graph.find(pending_abstraction_ids[j])) {
                check_and_add_dependency(
                    dependency_graph, pending_abstraction_ids[i],
                    pending_abstraction_ids[j], remaining_costs);
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
    if (time_connected_components) {
        cc_generation.stop();
        cc_computation.resume();
    }

    // Compute the connected components of the dependency graph.
    vector<vector<int>> independent_abstractions =
        dependency_graph.get_connected_components(pending_abstraction_ids);

    for (vector<int> &component : independent_abstractions) {
        sort(component.begin(), component.end());
    }
    sort(independent_abstractions.begin(), independent_abstractions.end());
    if (time_connected_components) {
        cc_computation.stop();
    }
    return independent_abstractions;
}

void StructuredSCPOrderGenerator::check_and_add_dependency(
    ccp::DisjointSet &dependency_graph, int id1, int id2,
    const Costs &remaining_costs) {
    assert(id1 < id2);

    if (g_hacked_use_affecting_labels) {
        const gtl::bit_vector *conflicting_ops_of_abstractions = nullptr;
        if (precomputed_conflicting_ops) {
            conflicting_ops_of_abstractions = &conflicting_ops[id1][id2];
        } else {
            const gtl::bit_vector &relevant_ops1 =
                relevant_ops_by_abstraction[id1];
            const gtl::bit_vector &relevant_ops2 =
                relevant_ops_by_abstraction[id2];
            conflicting_ops[0][0] = relevant_ops1 | relevant_ops2;
            conflicting_ops_of_abstractions = &conflicting_ops[0][0];
        }
        size_t op_id = conflicting_ops_of_abstractions->find_first();
        while (op_id != gtl::bit_vector::npos) {
            /* If this operator has infinite remaining cost, it does not
               affect any abstraction. */
            if (g_hacked_use_infinite_labels &&
                remaining_costs[op_id] == INF) {
                op_id = conflicting_ops_of_abstractions->find_next(op_id + 1);
                continue;
            }
            /* If this operator has remaining cost 0 and both abstractions
               guarantee that the remaining costs stay 0, it does not affect
               the order of these abstractions. */
            if (g_hacked_use_non_negative_labels &&
                remaining_costs[op_id] == 0 &&
                op_has_nonincreasing_remaining_costs[id1][op_id] &&
                op_has_nonincreasing_remaining_costs[id2][op_id]) {
                op_id = conflicting_ops_of_abstractions->find_next(op_id + 1);
                continue;
            }
            dependency_graph.unite(id1, id2);
            break;
        }
    } else {
        int num_operators = task_proxy.get_operators().size();
        for (int op_id = 0; op_id < num_operators; ++op_id) {
            if (g_hacked_use_infinite_labels &&
                remaining_costs[op_id] == INF) {
                continue;
            }
            if (g_hacked_use_non_negative_labels &&
                remaining_costs[op_id] == 0 &&
                op_has_nonincreasing_remaining_costs[id1][op_id] &&
                op_has_nonincreasing_remaining_costs[id2][op_id]) {
                continue;
            }
            dependency_graph.unite(id1, id2);
            break;
        }
    }
}

namespace {
void collect_nodes(
    const shared_ptr<SSCPNode> &node, vector<bool> &marked,
    vector<shared_ptr<SSCPNode>> &nodes) {
    int index = node->index;
    assert(index < static_cast<int>(marked.size()));
    if (index >= 0 && !marked[index]) {
        marked[index] = true;
        nodes.push_back(node);
        for (const shared_ptr<SSCPNode> &child : node->children) {
            assert(child);
            collect_nodes(child, marked, nodes);
        }
    }
}
}

StructuredSCPOrder StructuredSCPOrderGenerator::create_structured_scp_order(
    shared_ptr<SSCPNode> &root_node) {
    // Determine reachable nodes and order them by level.
    vector<shared_ptr<SSCPNode>> reachable_compositional_nodes;
    reachable_compositional_nodes.reserve(SSCPNode::num_compositional_nodes);
    vector<bool> marked(SSCPNode::num_compositional_nodes, false);
    if (root_node) {
        collect_nodes(root_node, marked, reachable_compositional_nodes);
        sort(reachable_compositional_nodes.begin(),
             reachable_compositional_nodes.end(),
             [](const shared_ptr<SSCPNode> &lhs,
                const shared_ptr<SSCPNode> &rhs) {
                 return lhs->level < rhs->level;
             });
    }
    cout << "Unreachable compositional nodes: "
         << SSCPNode::num_compositional_nodes -
        reachable_compositional_nodes.size()
         << endl;

    // Create compact lookup tables and extract unsolvability information.
    create_compact_lookup_tables();
    lookup_tables_cache = {};
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

    // Assign final indices to lookup nodes.
    int value_id = 0;
    for (vector<shared_ptr<LookupSSCPNode>> &lookup_nodes_by_abstraction :
         lookup_sscp_node_cache) {
        for (shared_ptr<LookupSSCPNode> &node : lookup_nodes_by_abstraction) {
            assert(node->index < 0);
            node->index = value_id;
            ++value_id;
        }
    }

    // Assign final indices to compositional nodes and create instructions.
    vector<Instruction> instructions;
    instructions.reserve(reachable_compositional_nodes.size());
    int num_reachable_nodes =
        static_cast<int>(reachable_compositional_nodes.size()) +
        SSCPNode::num_lookup_nodes;
    int num_reachable_sum_nodes = 0;
    int num_reachable_non_trivial_sum_nodes = 0;
    int num_reachable_max_nodes = 0;
    for (shared_ptr<SSCPNode> &node : reachable_compositional_nodes) {
        assert(!node->children.empty());
        node->index = value_id;
        ++value_id;

        vector<int> ids;
        ids.reserve(node->children.size());
        for (const shared_ptr<SSCPNode> &child : node->children) {
            assert(child->index < node->index);
            ids.push_back(child->index);
        }
        InstructionType type;
        if (dynamic_pointer_cast<MaxSSCPNode>(node)) {
            type = InstructionType::MAX;
            ++num_reachable_max_nodes;
        } else {
            assert(dynamic_pointer_cast<SumSSCPNode>(node));
            type = InstructionType::SUM;
            ++num_reachable_sum_nodes;
            if (dynamic_pointer_cast<SumSSCPNode>(node)->is_non_trivial()) {
                ++num_reachable_non_trivial_sum_nodes;
            }
        }
        instructions.emplace_back(type, move(ids));
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
        if (use_unsolvability) {
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

Costs StructuredSCPOrderGenerator::get_saturated_costs(
    const shared_ptr<LookupSSCPNode> &node) const {
    if (g_hacked_cache_scf_functions) {
        // Lookup node indices start at -1 and decrease.
        assert(utils::in_bounds(-node->index - 1, scf_cache));
        return scf_cache[-node->index - 1];
    } else {
        return compute_scf(
            *abstractions[node->abstraction_id], get_lookup_table(node),
            use_general_cp);
    }
}

Costs StructuredSCPOrderGenerator::compute_remaining_costs(
    const shared_ptr<LookupSSCPNode> &node, const Costs &costs) const {
    Costs remaining_costs(costs);
    reduce_costs(remaining_costs, get_saturated_costs(node));
    return remaining_costs;
}

/* Return the key under which the given cost function is registered in
   cost_key_cache, so that all other hash maps can use the small key instead
   of the full cost function. */
CostKey StructuredSCPOrderGenerator::lookup_costs_or_register(
    const Costs &costs) {
    const auto &it = cost_key_cache.find(costs);
    if (it != cost_key_cache.end()) {
        return it->second;
    } else {
        CostKey key = cost_key_cache.size();
        cost_key_cache.emplace(costs, key);
        return key;
    }
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
        "collect_time",
        "time the connected component construction and computation",
        "false");
    utils::add_log_options_to_feature(feature);
}

tuple<shared_ptr<AbstractTask>, Abstractions, bool, bool, utils::Verbosity>
get_structured_scp_order_generator_arguments_from_options(
    const plugins::Options &opts) {
    cout << "Generating abstractions..." << endl;
    Abstractions abstractions = generate_abstractions(
        opts.get<shared_ptr<AbstractTask>>("transform"),
        opts.get<vector<shared_ptr<AbstractionGenerator>>>(
            "abstraction_generators"));
    cout << "Generated " << abstractions.size() << " abstractions" << endl;
    g_hacked_max_lookup_table_cache_resizes =
        opts.get<int>("max_lookup_table_cache_resizes");
    return tuple_cat(
        forward_as_tuple(
            opts.get<shared_ptr<AbstractTask>>("transform"),
            move(abstractions),
            opts.get<bool>("use_unsolvability_infos"),
            opts.get<bool>("use_general_cp")),
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

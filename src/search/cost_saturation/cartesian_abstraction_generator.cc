#include "cartesian_abstraction_generator.h"

#include "cartesian_abstraction.h"
#include "explicit_abstraction.h"
#include "types.h"

#include "../cartesian_abstractions/abstraction.h"
#include "../cartesian_abstractions/abstract_state.h"
#include "../cartesian_abstractions/cegar.h"
#include "../cartesian_abstractions/cost_saturation.h"
#include "../cartesian_abstractions/refinement_hierarchy.h"
#include "../cartesian_abstractions/split_selector.h"
#include "../cartesian_abstractions/subtask_generators.h"
#include "../cartesian_abstractions/transition_system.h"
#include "../cartesian_abstractions/utils.h"
#include "../plugins/plugin.h"
#include "../utils/memory.h"
#include "../utils/rng_options.h"
#include "../task_utils/task_properties.h"
#include <cassert>
#include <cstddef>
#include <parallel_hashmap/phmap.h>
#include <unordered_set>

using namespace std;

namespace cost_saturation {
phmap::flat_hash_map<vector<int>, int, VectorHash> ops_to_label_id;
phmap::flat_hash_map<int, Label> label_id_to_label;
int next_label_id = -1;

static vector<vector<Successor>> get_backward_graph(
    const cartesian_abstractions::Abstraction &cartesian_abstraction,
    const vector<int> &h_values) {
    // Retrieve non-looping transitions.
    vector<vector<Successor>> backward_graph(cartesian_abstraction.get_num_states());
    for (int target = 0; target < cartesian_abstraction.get_num_states(); ++target) {
        // Prune transitions *to* unsolvable states.
        if (h_values[target] == INF) {
            continue;
        }
        for (const cartesian_abstractions::Transition &transition :
             cartesian_abstraction.get_incoming_transitions(target)) {
            int src = transition.target_id;
            // Prune transitions *from* unsolvable states.
            if (h_values[src] == INF) {
                continue;
            }
            backward_graph[target].emplace_back(transition.op_id, src);
        }
        backward_graph[target].shrink_to_fit();
    }
    return backward_graph;
}

static vector<vector<Successor>> get_backward_graph_w_lr(
    const cartesian_abstractions::Abstraction &cartesian_abstraction,
    const vector<int> &h_values,
	const vector<int> &cost, int &num_transitions_sub, int &num_single_transitions, unordered_set<int> &op_set, unordered_set<int> &op_set_single, int &num_label, int &num_new_label) {
    // Retrieve non-looping transitions.
    vector<vector<Successor>> backward_graph(cartesian_abstraction.get_num_states());

    // Map from (src, target) to list of operators
    map<pair<int, int>, vector<int>> transition_groups;
    for (int target = 0; target < cartesian_abstraction.get_num_states(); ++target) {
        // Prune transitions *to* unsolvable states.
        if (h_values[target] == INF) {
            continue;
        }
        for (const cartesian_abstractions::Transition &transition :
            cartesian_abstraction.get_incoming_transitions(target)) {
            num_transitions_sub++;
            int src = transition.target_id;
            // Prune transitions *from* unsolvable states.
            if (h_values[src] == INF) {
                continue;
            }
            transition_groups[{src, target}].push_back(transition.op_id);
        }
    }
    for (const auto &[src_target, op_list] : transition_groups) {
        const auto &[src, target] = src_target;
        
        vector<int> ops = op_list;
        sort(ops.begin(), ops.end());
        
        int label_id;
        if (ops.size() == 1) { // Param for min_label_size
            label_id = ops[0];
            // cout << "Operator ID: " << label_id << endl;
            num_single_transitions++;
			op_set_single.insert(label_id);
			op_set.insert(label_id);
        } else {
            num_label++;
            if (ops_to_label_id.count(ops)) {
                label_id = ops_to_label_id[ops];
            } else {
                label_id = next_label_id--;
                int label_cost = INF;
                for (int op_id : ops) {
                    label_cost = min(label_cost, cost[op_id]); 
					op_set.insert(op_id);
                }
                assert(label_cost >= 0);
                label_id_to_label[label_id] = Label(move(ops), label_cost);
                ops_to_label_id[ops] = label_id;
                num_new_label++;
            }
        }
        backward_graph[target].emplace_back(label_id, src);
    }    
    for (int target = 0; target < cartesian_abstraction.get_num_states(); ++target) {
        backward_graph[target].shrink_to_fit();
    }
    return backward_graph;
}

CartesianAbstractionGenerator::CartesianAbstractionGenerator(
    const vector<shared_ptr<cartesian_abstractions::SubtaskGenerator>> &subtasks,
    int max_states, int max_transitions, double max_time,
    cartesian_abstractions::PickFlawedAbstractState pick_flawed_abstract_state,
    cartesian_abstractions::PickSplit pick_split,
    cartesian_abstractions::PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state, int max_state_expansions,
    cartesian_abstractions::TransitionRepresentation transition_representation,
    int memory_padding, int random_seed,
    cartesian_abstractions::DotGraphVerbosity dot_graph_verbosity,
    utils::Verbosity verbosity)
    : AbstractionGenerator(verbosity),
      subtask_generators(subtasks),
      max_states(max_states),
      max_transitions(max_transitions),
      max_time(max_time),
      transition_representation(transition_representation),
      pick_flawed_abstract_state(pick_flawed_abstract_state),
      pick_split(pick_split),
      tiebreak_split(tiebreak_split),
      max_concrete_states_per_abstract_state(max_concrete_states_per_abstract_state),
      max_state_expansions(max_state_expansions),
      extra_memory_padding_mb(memory_padding),
      rng(utils::get_rng(random_seed)),
      dot_graph_verbosity(dot_graph_verbosity),
      num_states(0),
      num_transitions(0) {
}

bool CartesianAbstractionGenerator::has_reached_resource_limit(
    const utils::CountdownTimer &timer) const {
    return num_states >= max_states ||
           num_transitions >= max_transitions ||
           timer.is_expired() ||
           !utils::extra_memory_padding_is_reserved();
}

void CartesianAbstractionGenerator::build_abstractions_for_subtasks(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const utils::CountdownTimer &timer,
    Abstractions &abstractions) {
    log << "Build abstractions for " << subtasks.size() << " subtasks in "
        << timer.get_remaining_time() << endl;
    int remaining_subtasks = subtasks.size();
    for (const shared_ptr<AbstractTask> &subtask : subtasks) {
		log << "Abstraction===========================================================" << endl;
		int num_transitions_sub = 0;
		int num_single_transitions = 0;
		unordered_set<int> op_set;
		unordered_set<int> op_set_single;
		int num_label = 0;
		int num_new_label = 0;
        TaskProxy task_proxy(*subtask);
        vector<int> costs = task_properties::get_operator_costs(task_proxy);
        auto cegar = make_unique<cartesian_abstractions::CEGAR>(
            subtask,
            cartesian_abstractions::get_subtask_limit(
                max_states, num_states, remaining_subtasks),
            cartesian_abstractions::get_subtask_limit(
                max_transitions, num_transitions, remaining_subtasks),
            timer.get_remaining_time() / remaining_subtasks,
            pick_flawed_abstract_state,
            pick_split,
            tiebreak_split,
            max_concrete_states_per_abstract_state,
            max_state_expansions,
            transition_representation,
            *rng,
            log,
            dot_graph_verbosity);
        cout << endl;
        auto cartesian_abstraction = cegar->extract_abstraction();
        // If the timer expired, the goal distances might only be lower bounds.
        vector<int> goal_distances = cegar->get_goal_distances();
        cegar.release();  // Release memory for shortest paths, flaw search, etc.

        /* If we run out of memory while building an abstraction, we discard it
           to avoid running out of memory during the abstraction conversion. */
        if (!utils::extra_memory_padding_is_reserved()) {
            break;
        }

        num_states += cartesian_abstraction->get_num_states();
        int initial_state_id = cartesian_abstraction->get_initial_state().get_id();
        bool unsolvable = goal_distances[initial_state_id] == INF;

        unique_ptr<Abstraction> abstraction;
        if (transition_representation == cartesian_abstractions::TransitionRepresentation::STORE) {
            auto backward_graph = get_backward_graph(*cartesian_abstraction, goal_distances);
            // auto backward_graph = get_backward_graph_w_lr(*cartesian_abstraction, goal_distances, 
				// costs, num_transitions_sub, num_single_transitions, op_set, op_set_single, num_label, num_new_label);
				for (size_t target = 0; target < backward_graph.size(); ++target) {
					log << "Graph: " << target << backward_graph[target] << endl;
				}
				for (const auto &transitions : backward_graph) {
					num_transitions += transitions.size();
				}
				abstraction = make_unique<ExplicitAbstraction>(
					make_unique<CartesianAbstractionFunction>(
                    cartesian_abstraction->extract_refinement_hierarchy()),
                move(backward_graph),
                cartesian_abstraction->get_looping_operators(),
                vector<int>(
                    cartesian_abstraction->get_goals().begin(),
                    cartesian_abstraction->get_goals().end()));
        } else {
			abstraction = make_unique<CartesianAbstraction>(move(cartesian_abstraction));
        }
        abstractions.push_back(move(abstraction));
		
        if (has_reached_resource_limit(timer) || unsolvable) {
			break;
        }
		
        --remaining_subtasks;
		// for (const auto &pair : label_id_to_label) {
		// 	int label_id = pair.first;
		// 	const Label &label = pair.second;
		// 	log << "Label ID " << label_id << ": [";
		// 	for (size_t i = 0; i < label.operators.size(); ++i) {
		// 		log << label.operators[i];
		// 		if (i < label.operators.size() - 1)
		// 		log << ", ";
		// }
		// log << "] (cost = " << label.cost << ")" << endl;
		// }
		log << "Number of transitions: " << num_transitions_sub << endl;
		log << "Number of single transitions: " << num_single_transitions << endl;
		log << "Number of unique operators: " << op_set.size() << endl;
		log << "Number of unique, single operators: " << op_set_single.size() << endl;
		log << "Number of labels: " << num_label << endl;
		log << "Number of globally new labels: " << num_new_label << endl;
		log << "Number of locally reused labels: " << num_label - num_new_label << endl;
		log << "Change in transitions ((#single transitions+#labels)/#transitions): " << 
		static_cast<double>(num_single_transitions+num_label)/num_transitions_sub << endl;

		num_total_single_transitions+=num_single_transitions;
		num_total_reused_labels+=num_label - num_new_label;
    }
}

Abstractions CartesianAbstractionGenerator::generate_abstractions(
    const shared_ptr<AbstractTask> &task,
    DeadEnds *) {
    utils::CountdownTimer timer(max_time);
    num_states = 0;
    num_transitions = 0;
	num_total_single_transitions = 0;
	num_total_reused_labels = 0;
    log << "Build Cartesian abstractions" << endl << endl;

    // The CEGAR code expects that some extra memory is reserved.
    utils::reserve_extra_memory_padding(extra_memory_padding_mb);

    Abstractions abstractions;
    for (const auto &subtask_generator : subtask_generators) {
        cartesian_abstractions::SharedTasks subtasks = subtask_generator->get_subtasks(task, log);
        build_abstractions_for_subtasks(subtasks, timer, abstractions);
        if (has_reached_resource_limit(timer)) {
            break;
        }
    }

    if (utils::extra_memory_padding_is_reserved()) {
        utils::release_extra_memory_padding();
    }

    log << "Cartesian abstractions: " << abstractions.size() << endl;
    log << "Time for building Cartesian abstractions: "
        << timer.get_elapsed_time() << endl;
    log << "Total number of Cartesian states: " << num_states << endl;
    log << "Total number of transitions in Cartesian abstractions: " << num_transitions << endl;
    log << "Total number of single transitions in Cartesian abstractions: " << num_total_single_transitions << endl;
    log << "Total number of operators in Cartesian abstractions: "
    << task->get_num_operators() << endl;
    log << "Total number of labels in Cartesian abstractions: " << label_id_to_label.size() << endl;
    log << "Total number of reused labels in Cartesian abstractions: " << num_total_reused_labels << endl;
    log << "Total change in transitions ((#single transitions+#labels)/#transitions): " << 
	static_cast<double>(num_total_single_transitions+label_id_to_label.size())/num_transitions << endl;
    if (!label_id_to_label.empty()) {
        int min_size = 2;
        int max_size = 2;
        int total_size = 0;
        map<int, int> label_size_counts;
    
        for (const auto& pair : label_id_to_label) {
            const Label &label = pair.second;
            int label_size = label.operators.size();
            min_size = min(min_size, label_size);
            max_size = max(max_size, label_size);
            total_size += label_size;
            label_size_counts[label_size]++;
        }

        double average_size = static_cast<double>(total_size) / label_id_to_label.size();
    
        log << "Label sizes: min = " << min_size 
                << ", max = " << max_size
                << ", avg = " << average_size 
                << endl;

        for (const auto &[size, count] : label_size_counts) {
            log << "Labels of size " << size << ": " << count << std::endl;
        }       
    }
    return abstractions;
}

class CartesianAbstractionGeneratorFeature
    : public plugins::TypedFeature<AbstractionGenerator, CartesianAbstractionGenerator> {
public:
    CartesianAbstractionGeneratorFeature() : TypedFeature("cartesian") {
        document_title("Cartesian abstraction generator");
        cartesian_abstractions::add_common_cegar_options(*this);
        utils::add_log_options_to_feature(*this);
    }

    virtual shared_ptr<CartesianAbstractionGenerator> create_component(
        const plugins::Options &opts) const override {
        cartesian_abstractions::g_hacked_sort_transitions = opts.get<bool>("sort_transitions");

        return plugins::make_shared_from_arg_tuples<CartesianAbstractionGenerator>(
            opts.get_list<shared_ptr<cartesian_abstractions::SubtaskGenerator>>("subtasks"),
            opts.get<int>("max_states"),
            opts.get<int>("max_transitions"),
            opts.get<double>("max_time"),
            opts.get<cartesian_abstractions::PickFlawedAbstractState>("pick_flawed_abstract_state"),
            opts.get<cartesian_abstractions::PickSplit>("pick_split"),
            opts.get<cartesian_abstractions::PickSplit>("tiebreak_split"),
            opts.get<int>("max_concrete_states_per_abstract_state"),
            opts.get<int>("max_state_expansions"),
            opts.get<cartesian_abstractions::TransitionRepresentation>("transition_representation"),
            opts.get<int>("memory_padding"),
            utils::get_rng_arguments_from_options(opts),
            opts.get<cartesian_abstractions::DotGraphVerbosity>("dot_graph_verbosity"),
            opts.get<utils::Verbosity>("verbosity"));
    }
};

static plugins::FeaturePlugin<CartesianAbstractionGeneratorFeature> _plugin;
}

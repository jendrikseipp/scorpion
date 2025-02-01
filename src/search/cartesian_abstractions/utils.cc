#include "utils.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "flaw_search.h"
#include "split_selector.h"
#include "transition.h"
#include "transition_system.h"

#include "../plugins/plugin.h"
#include "../heuristics/additive_heuristic.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

using namespace std;

namespace cartesian_abstractions {
class SubtaskGenerator;
int g_hacked_extra_memory_padding_mb = 512;
bool g_hacked_sort_transitions = false;
bool g_hacked_use_abstract_flaw_search = false;
TransitionRepresentation g_hacked_tsr = TransitionRepresentation::STORE;

static bool operator_applicable(
    const OperatorProxy &op, const utils::HashSet<FactProxy> &facts) {
    for (FactProxy precondition : op.get_preconditions()) {
        if (facts.count(precondition) == 0)
            return false;
    }
    return true;
}

static bool operator_achieves_fact(
    const OperatorProxy &op, const FactProxy &fact) {
    for (EffectProxy effect : op.get_effects()) {
        if (effect.get_fact() == fact)
            return true;
    }
    return false;
}

static utils::HashSet<FactProxy> compute_possibly_before_facts(
    const TaskProxy &task, const FactProxy &last_fact) {
    utils::HashSet<FactProxy> pb_facts;

    // Add facts from initial state.
    for (FactProxy fact : task.get_initial_state())
        pb_facts.insert(fact);

    // Until no more facts can be added:
    size_t last_num_reached = 0;
    /*
      Note: This can be done more efficiently by maintaining the number
      of unsatisfied preconditions for each operator and a queue of
      unhandled effects.

      TODO: Find out if this code is time critical, and change it if it
      is.
    */
    while (last_num_reached != pb_facts.size()) {
        last_num_reached = pb_facts.size();
        for (OperatorProxy op : task.get_operators()) {
            // Ignore operators that achieve last_fact.
            if (operator_achieves_fact(op, last_fact))
                continue;
            // Add all facts that are achieved by an applicable operator.
            if (operator_applicable(op, pb_facts)) {
                for (EffectProxy effect : op.get_effects()) {
                    pb_facts.insert(effect.get_fact());
                }
            }
        }
    }
    return pb_facts;
}

utils::HashSet<FactProxy> get_relaxed_possible_before(
    const TaskProxy &task, const FactProxy &fact) {
    utils::HashSet<FactProxy> reachable_facts =
        compute_possibly_before_facts(task, fact);
    reachable_facts.insert(fact);
    return reachable_facts;
}

vector<int> get_domain_sizes(const TaskProxy &task) {
    vector<int> domain_sizes;
    for (VariableProxy var : task.get_variables())
        domain_sizes.push_back(var.get_domain_size());
    return domain_sizes;
}

static void add_pick_flawed_abstract_state_strategies(plugins::Feature &feature) {
    feature.add_option<cartesian_abstractions::PickFlawedAbstractState>(
        "pick_flawed_abstract_state",
        "flaw-selection strategy",
        "batch_min_h");
}

static void add_pick_split_strategies(plugins::Feature &feature) {
    feature.add_option<PickSplit>(
        "pick_split",
        "split-selection strategy",
        "max_cover");
    feature.add_option<PickSplit>(
        "tiebreak_split",
        "split-selection strategy for breaking ties",
        "max_refined");
}

static void add_memory_padding_option(plugins::Feature &feature) {
    feature.add_option<int>(
        "memory_padding",
        "amount of extra memory in MB to reserve for recovering from "
        "out-of-memory situations gracefully. When the memory runs out, we "
        "stop refining and start the search. Due to memory fragmentation, "
        "the memory used for building the abstraction (states, transitions, "
        "etc.) often can't be reused for things that require big continuous "
        "blocks of memory. It is for this reason that we require a rather "
        "large amount of memory padding by default.",
        "500",
        plugins::Bounds("0", "infinity"));
}

static void add_dot_graph_verbosity(plugins::Feature &feature) {
    feature.add_option<DotGraphVerbosity>(
        "dot_graph_verbosity",
        "verbosity of printing/writing dot graphs",
        "silent");
}

static void add_transition_representation_option(plugins::Feature &feature) {
    feature.add_option<TransitionRepresentation>(
        "transition_representation",
        "how to compute transitions between abstract states",
        "store");
}

string create_dot_graph(const TaskProxy &task_proxy, const Abstraction &abstraction) {
    ostringstream oss;
    int num_states = abstraction.get_num_states();
    oss << "digraph transition_system";
    oss << " {" << endl;
    oss << "    node [shape = none] start;" << endl;
    for (int i = 0; i < num_states; ++i) {
        bool is_init = (i == abstraction.get_initial_state().get_id());
        bool is_goal = abstraction.get_goals().count(i);
        oss << "    node [shape = " << (is_goal ? "doublecircle" : "circle")
            << "] " << i << ";" << endl;
        if (is_init)
            oss << "    start -> " << i << ";" << endl;
    }
    for (int state_id = 0; state_id < num_states; ++state_id) {
        map<int, vector<int>> parallel_transitions;
        for (const Transition &t : abstraction.get_outgoing_transitions(state_id)) {
            parallel_transitions[t.target_id].push_back(t.op_id);
        }
        for (auto &pair : parallel_transitions) {
            int target = pair.first;
            vector<int> &operators = pair.second;
            sort(operators.begin(), operators.end());
            vector<string> operator_names;
            operator_names.reserve(operators.size());
            for (int op_id : operators) {
                operator_names.push_back(task_proxy.get_operators()[op_id].get_name());
            }
            oss << "    " << state_id << " -> " << target << " [label = \""
                << utils::join(operator_names, ", ") << "\"];" << endl;
        }
    }
    oss << "}" << endl;
    return oss.str();
}

void write_to_file(const string &file_name, const string &content) {
    ofstream output_file(file_name);
    if (output_file.is_open()) {
        output_file << content;
        output_file.close();
    } else {
        ABORT("failed to open " + file_name);
    }
    if (output_file.fail()) {
        ABORT("failed to write to " + file_name);
    }
}

void add_common_cegar_options(plugins::Feature &feature) {
    feature.add_list_option<shared_ptr<SubtaskGenerator>>(
        "subtasks",
        "subtask generators",
        "[landmarks(order=random), goals(order=random)]");
    feature.add_option<int>(
        "max_states",
        "maximum sum of abstract states over all abstractions",
        "infinity",
        plugins::Bounds("1", "infinity"));
    feature.add_option<int>(
        "max_transitions",
        "maximum sum of state-changing transitions (excluding self-loops) over "
        "all abstractions",
        "1M",
        plugins::Bounds("0", "infinity"));
    feature.add_option<double>(
        "max_time",
        "maximum time in seconds for building abstractions",
        "infinity",
        plugins::Bounds("0.0", "infinity"));
    feature.add_option<bool>(
        "use_max",
        "compute maximum over heuristic estimates instead of SCP",
        "false");
    feature.add_option<bool>(
        "sort_transitions",
        "sort transitions",
        "false");
    feature.add_option<bool>(
        "use_abstract_flaw_search",
        "let the flaw search expand all concrete states belonging to an abstract state at once",
        "false");
    feature.add_option<bool>(
        "store_shortest_path_tree_children",
        "store for each state its children in the shortest path tree",
        "false");
    feature.add_option<bool>(
        "store_shortest_path_tree_parents",
        "store for each state its parents in the shortest path tree",
        "false");

    add_transition_representation_option(feature);
    add_pick_flawed_abstract_state_strategies(feature);
    add_pick_split_strategies(feature);
    feature.add_option<int>(
        "max_concrete_states_per_abstract_state",
        "maximum number of flawed concrete states stored per abstract state",
        "infinity",
        plugins::Bounds("1", "infinity"));
    feature.add_option<int>(
        "max_state_expansions",
        "maximum number of state expansions per flaw search if a flaw has already been found",
        "1M",
        plugins::Bounds("1", "infinity"));
    add_memory_padding_option(feature);
    utils::add_rng_options_to_feature(feature);
    add_dot_graph_verbosity(feature);
}

static plugins::TypedEnumPlugin<DotGraphVerbosity> _enum_plugin_dot_graph_verbosity({
        {"silent", ""},
        {"write_to_console", ""},
        {"write_to_file", ""}
    });

static plugins::TypedEnumPlugin<TransitionRepresentation> _enum_plugin_transition_representation({
        {"store", "store transitions"},
        {"naive", "compute applicable operators by looping over all operators and transitions by looping over all abstract states"},
        {"sg", "compute operators via successor generator and transitions naively"},
        {"rh", "compute operators naively and transitions via refinement hierarchy"},
        {"sg_rh", "compute operators via successor generator and transitions via refinement hierarchy"},
        {"store_then_sg_rh", "start with storing transitions until running out of memory, then compute them on demand"},
    });
}

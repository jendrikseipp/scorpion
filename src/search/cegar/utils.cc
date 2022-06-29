#include "utils.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "flaw_search.h"
#include "split_selector.h"
#include "transition.h"
#include "transition_system.h"

#include "../option_parser.h"

#include "../heuristics/additive_heuristic.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng_options.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>

using namespace std;

namespace cegar {
class SubtaskGenerator;

unique_ptr<additive_heuristic::AdditiveHeuristic> create_additive_heuristic(
    const shared_ptr<AbstractTask> &task) {
    Options opts;
    opts.set<shared_ptr<AbstractTask>>("transform", task);
    opts.set<bool>("cache_estimates", false);
    opts.set<utils::Verbosity>("verbosity", utils::Verbosity::SILENT);
    return utils::make_unique_ptr<additive_heuristic::AdditiveHeuristic>(opts);
}

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

static void add_pick_flawed_abstract_state_strategies(options::OptionParser &parser) {
    parser.add_enum_option<cegar::PickFlawedAbstractState>(
        "pick_flawed_abstract_state",
        {"FIRST", "FIRST_ON_SHORTEST_PATH", "RANDOM", "MIN_H", "MAX_H", "BATCH_MIN_H"},
        "flaw-selection strategy",
        "BATCH_MIN_H");
}

static void add_pick_split_strategies(options::OptionParser &parser) {
    vector<string> strategies =
    {"RANDOM", "MIN_UNWANTED", "MAX_UNWANTED", "MIN_REFINED", "MAX_REFINED",
     "MIN_HADD", "MAX_HADD", "MIN_CG", "MAX_CG", "MAX_COVER"};
    parser.add_enum_option<PickSplit>(
        "pick_split",
        strategies,
        "split-selection strategy",
        "MAX_COVER");
    parser.add_enum_option<PickSplit>(
        "tiebreak_split",
        strategies,
        "split-selection strategy for breaking ties",
        "MAX_REFINED");
}

static void add_search_strategy_option(options::OptionParser &parser) {
    parser.add_enum_option<SearchStrategy>(
        "search_strategy",
        {"ASTAR", "INCREMENTAL"},
        "strategy for computing abstract plans",
        "INCREMENTAL");
}

static void add_memory_padding_option(options::OptionParser &parser) {
    parser.add_option<int>(
        "memory_padding",
        "amount of extra memory in MB to reserve for recovering from "
        "out-of-memory situations gracefully. When the memory runs out, we "
        "stop refining and start the search. Due to memory fragmentation, "
        "the memory used for building the abstraction (states, transitions, "
        "etc.) often can't be reused for things that require big continuous "
        "blocks of memory. It is for this reason that we require a rather "
        "large amount of memory padding by default.",
        "500",
        Bounds("0", "infinity"));
}

static void add_dot_graph_verbosity(options::OptionParser &parser) {
    parser.add_enum_option<DotGraphVerbosity>(
        "dot_graph_verbosity",
        {"SILENT", "WRITE_TO_CONSOLE", "WRITE_TO_FILE"},
        "verbosity of printing/writing dot graphs",
        "SILENT");
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
        auto transitions =
            abstraction.get_transition_system().get_outgoing_transitions();
        for (const Transition &t : transitions[state_id]) {
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

void add_common_cegar_options(options::OptionParser &parser) {
    parser.add_list_option<shared_ptr<SubtaskGenerator>>(
        "subtasks",
        "subtask generators",
        "[landmarks(order=random), goals(order=random)]");
    parser.add_option<int>(
        "max_states",
        "maximum sum of abstract states over all abstractions",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_transitions",
        "maximum sum of state-changing transitions (excluding self-loops) over "
        "all abstractions",
        "1M",
        Bounds("0", "infinity"));
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds for building abstractions",
        "infinity",
        Bounds("0.0", "infinity"));

    add_pick_flawed_abstract_state_strategies(parser);
    add_pick_split_strategies(parser);
    add_search_strategy_option(parser);
    add_memory_padding_option(parser);
    add_dot_graph_verbosity(parser);
    utils::add_rng_options(parser);

    parser.add_option<int>(
        "max_concrete_states_per_abstract_state",
        "maximum number of flawed concrete states stored per abstract state",
        "infinity",
        Bounds("1", "infinity"));
    parser.add_option<int>(
        "max_state_expansions",
        "maximum number of state expansions per flaw search",
        "1M",
        Bounds("1", "infinity"));
}
}

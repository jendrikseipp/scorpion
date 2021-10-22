#include "utils.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "flaw_selector.h"
#include "transition.h"
#include "transition_system.h"

#include "../option_parser.h"

#include "../heuristics/additive_heuristic.h"
#include "../task_utils/task_properties.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>

using namespace std;

namespace cegar {
unique_ptr<additive_heuristic::AdditiveHeuristic>
create_additive_heuristic(const shared_ptr<AbstractTask> &task) {
    Options opts;
    opts.set<shared_ptr<AbstractTask>>("transform", task);
    opts.set<bool>("cache_estimates", false);
    return utils::make_unique_ptr<additive_heuristic::AdditiveHeuristic>(opts);
}

static bool operator_applicable(const OperatorProxy &op,
                                const utils::HashSet<FactProxy> &facts) {
    for (FactProxy precondition : op.get_preconditions()) {
        if (facts.count(precondition) == 0)
            return false;
    }
    return true;
}

static bool operator_achieves_fact(const OperatorProxy &op,
                                   const FactProxy &fact) {
    for (EffectProxy effect : op.get_effects()) {
        if (effect.get_fact() == fact)
            return true;
    }
    return false;
}

static utils::HashSet<FactProxy>
compute_possibly_before_facts(const TaskProxy &task,
                              const FactProxy &last_fact) {
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

utils::HashSet<FactProxy> get_relaxed_possible_before(const TaskProxy &task,
                                                      const FactProxy &fact) {
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

void add_search_strategy_option(options::OptionParser &parser) {
    parser.add_enum_option<SearchStrategy>(
        "search_strategy", {"ASTAR", "INCREMENTAL"},
        "strategy for computing abstract plans", "INCREMENTAL");
}

void add_memory_padding_option(options::OptionParser &parser) {
    parser.add_option<int>(
        "memory_padding",
        "amount of extra memory in MB to reserve for recovering from "
        "out-of-memory situations gracefully. When the memory runs out, we "
        "stop refining and start the search. Due to memory fragmentation, "
        "the memory used for building the abstraction (states, transitions, "
        "etc.) often can't be reused for things that require big continuous "
        "blocks of memory. It is for this reason that we require a rather "
        "large amount of memory padding by default.",
        "500", Bounds("0", "infinity"));
}

void add_flaw_strategy_option(options::OptionParser &parser) {
    parser.add_enum_option<FlawStrategy>(
        "flaw_strategy",
        {"BACKTRACK_OPTIMISTIC",
         "BACKTRACK_OPTIMISTIC_SLOW",
         "BACKTRACK_PESSIMISTIC",
         "BACKTRACK_PESSIMISTIC_SLOW",
         "OPTIMISTIC",
         "OPTIMISTIC_SLOW",
         "ORIGINAL",
         "PESSIMISTIC",
         "PESSIMISTIC_SLOW",
         "RANDOM",
         "SEARCH",
         "SEARCH_MULTIPLE_FLAWS"},
        "strategy to handle flaws", "ORIGINAL");
}

string get_dot_graph(const Abstraction &abstraction, const Solution &solution,
                     const TaskProxy &task) {
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
            oss << "    " << state_id << " -> " << target << " [label = \""
                << utils::join(operators, "_") << "\"];" << endl;
        }
    }

    // print solution
    int cur_state_id = abstraction.get_initial_state().get_id();
    for (const Transition &t : solution) {
        oss << "    " << cur_state_id << " -> " << t.target_id
            << " [color=red,fontcolor=red,label = \""
            << task.get_operators()[t.op_id].get_name() << " (" << t.op_id
            << ")\"];" << endl;
        cur_state_id = t.target_id;
    }
    oss << "}" << endl;
    return oss.str();
}

void dump_dot_graph(const Abstraction &abstraction, const Solution &solution,
                    const TaskProxy &task) {
    cout << get_dot_graph(abstraction, solution, task) << endl;
}

void write_dot_graph(const Abstraction &abstraction,
                     const Solution &solution,
                     const TaskProxy &task,
                     const string &file_name) {
    ofstream output_file(file_name);
    if (output_file.is_open()) {
        output_file << get_dot_graph(abstraction, solution, task);
    }
    output_file.close();
}

extern void handle_dot_graph(const Abstraction &abstraction,
                             const Solution &solution,
                             const TaskProxy &task,
                             const string &file_name,
                             const int dot_graph_verbosity) {
    if (dot_graph_verbosity == 1 || dot_graph_verbosity == 3) {
        dump_dot_graph(abstraction, solution, task);
    }
    if (dot_graph_verbosity > 1) {
        write_dot_graph(abstraction, solution, task, file_name);
    }
}
} // namespace cegar

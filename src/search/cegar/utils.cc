#include "utils.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "transition.h"
#include "transition_system.h"

#include "../option_parser.h"

#include "../heuristics/additive_heuristic.h"
#include "../task_utils/task_properties.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

using namespace std;

namespace cegar {
int g_hacked_extra_memory_padding_mb = 512;
OperatorOrdering g_hacked_operator_ordering = OperatorOrdering::ID_UP;
StateOrdering g_hacked_state_ordering = StateOrdering::STATE_ID_UP;
bool g_hacked_sort_transitions = false;
TransitionRepresentation g_hacked_tsr = TransitionRepresentation::TS;
std::shared_ptr<utils::RandomNumberGenerator> g_hacked_rng;

unique_ptr<additive_heuristic::AdditiveHeuristic> create_additive_heuristic(
    const shared_ptr<AbstractTask> &task) {
    Options opts;
    opts.set<shared_ptr<AbstractTask>>("transform", task);
    opts.set<bool>("cache_estimates", false);
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

void add_h_update_option(options::OptionParser &parser) {
    vector<string> h_update;
    h_update.push_back("STATES_ON_TRACE");
    h_update.push_back("DIJKSTRA_FROM_UNCONNECTED_ORPHANS");
    parser.add_enum_option<HUpdateStrategy>(
        "h_update",
        h_update,
        "strategy for updating goal distances or distance estimates",
        "DIJKSTRA_FROM_UNCONNECTED_ORPHANS");
}

void add_transition_representation_option(options::OptionParser &parser) {
    vector<string> options;
    options.push_back("TS");
    options.push_back("MT");
    options.push_back("SG");
    options.push_back("TS_THEN_SG");
    parser.add_enum_option<TransitionRepresentation>(
        "transition_representation",
        options,
        "how to compute transitions between abstract states",
        "TS");
}

void add_operator_ordering_option(options::OptionParser &parser) {
    vector<string> options;
    options.push_back("RANDOM");
    options.push_back("ID_UP");
    options.push_back("ID_DOWN");
    options.push_back("COST_UP");
    options.push_back("COST_DOWN");
    options.push_back("POSTCONDITIONS_UP");
    options.push_back("POSTCONDITIONS_DOWN");
    options.push_back("LAYER_UP");
    options.push_back("LAYER_DOWN");
    parser.add_enum_option<OperatorOrdering>(
        "operator_order",
        options,
        "how to order operators",
        "ID_UP");
}

void add_state_ordering_option(options::OptionParser &parser) {
    vector<string> options;
    options.push_back("NONE");
    options.push_back("RANDOM");
    options.push_back("STATE_ID_UP");
    options.push_back("STATE_ID_DOWN");
    options.push_back("NODE_ID_UP");
    options.push_back("NODE_ID_DOWN");
    options.push_back("SIZE_UP");
    options.push_back("SIZE_DOWN");
    parser.add_enum_option<StateOrdering>(
        "state_order",
        options,
        "how to order states",
        "STATE_ID_UP");
}

void dump_dot_graph(const Abstraction &abstraction) {
    int num_states = abstraction.get_num_states();
    cout << "digraph transition_system";
    cout << " {" << endl;
    cout << "    node [shape = none] start;" << endl;
    for (int i = 0; i < num_states; ++i) {
        bool is_init = (i == abstraction.get_initial_state().get_id());
        bool is_goal = abstraction.get_goals().count(i);
        cout << "    node [shape = " << (is_goal ? "doublecircle" : "circle")
             << "] " << i << ";" << endl;
        if (is_init)
            cout << "    start -> " << i << ";" << endl;
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
            cout << "    " << state_id << " -> " << target
                 << " [label = \"" << utils::join(operators, "_") << "\"];" << endl;
        }
    }
    cout << "}" << endl;
}

void write_dot_file_to_disk(const Abstraction &abstraction) {
    ostringstream name;
    name << internal << setfill('0') << setw(3) << abstraction.get_num_states();
    ofstream out;
    out.open("graph-" + name.str() + ".dot");
    int num_states = abstraction.get_num_states();
    out << "digraph transition_system";
    out << " {" << endl;
    out << "    node [shape = none] start;" << endl;
    for (int i = 0; i < num_states; ++i) {
        bool is_init = (i == abstraction.get_initial_state().get_id());
        bool is_goal = abstraction.get_goals().count(i);
        out << "    node [shape = " << (is_goal ? "doublecircle" : "circle")
            << "] " << i << ";" << endl;
        if (is_init)
            out << "    start -> " << i << ";" << endl;
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
            out << "    " << state_id << " -> " << target
                << " [label = \"" << utils::join(operators, "_") << "\"];" << endl;
        }
    }
    out << "}" << endl;
    out.close();
}
}

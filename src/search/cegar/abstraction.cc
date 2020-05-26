#include "abstraction.h"

#include "abstract_state.h"
#include "match_tree.h"
#include "refinement_hierarchy.h"
#include "transition.h"
#include "transition_system.h"
#include "utils.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/math.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace std;

namespace cegar {
Abstraction::Abstraction(const shared_ptr<AbstractTask> &task, bool debug)
    : concrete_initial_state(TaskProxy(*task).get_initial_state()),
      goal_facts(task_properties::get_fact_pairs(TaskProxy(*task).get_goals())),
      refinement_hierarchy(utils::make_unique_ptr<RefinementHierarchy>(task)),
      debug(debug) {
    initialize_trivial_abstraction(get_domain_sizes(TaskProxy(*task)));

    if (g_hacked_use_cartesian_match_tree) {
        match_tree = utils::make_unique_ptr<MatchTree>(
            TaskProxy(*task).get_operators(), *refinement_hierarchy, cartesian_sets, debug);
    } else {
        transition_system = utils::make_unique_ptr<TransitionSystem>(
            TaskProxy(*task).get_operators());
    }
#ifndef NDEBUG
    if (!transition_system) {
        transition_system = utils::make_unique_ptr<TransitionSystem>(
            TaskProxy(*task).get_operators());
    }
    if (!match_tree) {
        match_tree = utils::make_unique_ptr<MatchTree>(
            TaskProxy(*task).get_operators(), *refinement_hierarchy, cartesian_sets, debug);
    }
#endif
}

Abstraction::~Abstraction() {
}

const AbstractState &Abstraction::get_initial_state() const {
    return *states[init_id];
}

int Abstraction::get_num_states() const {
    return states.size();
}

const Goals &Abstraction::get_goals() const {
    return goals;
}

const AbstractState &Abstraction::get_state(int state_id) const {
    return *states[state_id];
}

const TransitionSystem &Abstraction::get_transition_system() const {
    return *transition_system;
}

unique_ptr<RefinementHierarchy> Abstraction::extract_refinement_hierarchy() {
    assert(refinement_hierarchy);
    return move(refinement_hierarchy);
}

int Abstraction::get_num_operators() const {
    if (match_tree) {
        return match_tree->get_num_operators();
    }
    return transition_system->get_num_operators();
}

int Abstraction::get_num_transitions() const {
    if (match_tree) {
        return 0;
    }
    return transition_system->get_num_non_loops();
}

Transitions Abstraction::get_incoming_transitions(int state_id, int cost) const {
    if (match_tree) {
        return match_tree->get_incoming_transitions(cartesian_sets, *states[state_id], cost);
    } else if (cost == -1) {
        return transition_system->get_incoming_transitions()[state_id];
    } else {
        OperatorsProxy operators = refinement_hierarchy->get_task_proxy().get_operators();
        Transitions filtered_transitions;
        for (const Transition &t : transition_system->get_incoming_transitions()[state_id]) {
            if (operators[t.op_id].get_cost() == cost) {
                filtered_transitions.push_back(t);
            }
        }
        return filtered_transitions;
    }
}

Transitions Abstraction::get_outgoing_transitions(int state_id) const {
    if (match_tree) {
        return match_tree->get_outgoing_transitions(cartesian_sets, *states[state_id]);
    }
    return transition_system->get_outgoing_transitions()[state_id];
}

void Abstraction::mark_all_states_as_goals() {
    goals.clear();
    for (auto &state : states) {
        goals.insert(state->get_id());
    }
}

void Abstraction::initialize_trivial_abstraction(const vector<int> &domain_sizes) {
    cartesian_sets.push_back(utils::make_unique_ptr<CartesianSet>(domain_sizes));
    unique_ptr<AbstractState> init_state =
        AbstractState::get_trivial_abstract_state(*cartesian_sets[0]);
    init_id = init_state->get_id();
    goals.insert(init_state->get_id());
    states.push_back(move(init_state));
}

pair<int, int> Abstraction::refine(
    const AbstractState &state, int var, const vector<int> &wanted) {
    if (debug)
        cout << "Refine " << state << " for " << var << "=" << wanted << endl;

    int v_id = state.get_id();
    // Reuse state ID from obsolete parent to obtain consecutive IDs.
    int v1_id = v_id;
    int v2_id = get_num_states();

    // Ensure that the initial state always has state ID 0.
    if (v_id == init_id &&
        count(wanted.begin(), wanted.end(), concrete_initial_state[var].get_value())) {
        swap(v1_id, v2_id);
    }

    // Update refinement hierarchy.
    pair<NodeID, NodeID> node_ids = refinement_hierarchy->split(
        state.get_node_id(), var, wanted, v1_id, v2_id);

    pair<CartesianSet, CartesianSet> cartesian_sets =
        state.split_domain(var, wanted);

    this->cartesian_sets.resize(max(node_ids.first, node_ids.second) + 1);
    this->cartesian_sets[node_ids.first] =
        utils::make_unique_ptr<CartesianSet>(move(cartesian_sets.first));
    this->cartesian_sets[node_ids.second] =
        utils::make_unique_ptr<CartesianSet>(move(cartesian_sets.second));

    unique_ptr<AbstractState> v1 = utils::make_unique_ptr<AbstractState>(
        v1_id, node_ids.first, *this->cartesian_sets[node_ids.first]);
    unique_ptr<AbstractState> v2 = utils::make_unique_ptr<AbstractState>(
        v2_id, node_ids.second, *this->cartesian_sets[node_ids.second]);
    assert(state.includes(*v1));
    assert(state.includes(*v2));

    if (goals.count(v_id)) {
        goals.erase(v_id);
        if (v1->includes(goal_facts)) {
            goals.insert(v1_id);
        }
        if (v2->includes(goal_facts)) {
            goals.insert(v2_id);
        }
        if (debug) {
            cout << "Number of goal states: " << goals.size() << endl;
        }
    }

    if (transition_system) {
        transition_system->rewire(states, v_id, *v1, *v2, var);
    }
    if (match_tree) {
        match_tree->split(this->cartesian_sets, state, var);
    }

    if (debug) {
        //transition_system->dump();
        //match_tree->dump();
    }

    states.emplace_back();
    states[v1_id] = move(v1);
    states[v2_id] = move(v2);

    assert(init_id == 0);
    assert(get_initial_state().includes(concrete_initial_state));

    if (debug) {
        //refinement_hierarchy->dump();
    }

#ifndef NDEBUG
    for (int state_id : {v1_id, v2_id}) {
        const AbstractState &state = *states[state_id];
        Transitions ts_out = transition_system->get_outgoing_transitions()[state_id];
        Transitions mt_out = match_tree->get_outgoing_transitions(this->cartesian_sets, state);
        sort(ts_out.begin(), ts_out.end());
        sort(mt_out.begin(), mt_out.end());
        if (ts_out != mt_out) {
            cout << "State " << state_id << ", node: " << state.get_node_id() << endl;
            cout << "  TS out: " << ts_out << endl;
            cout << "  MT out: " << mt_out << endl;
        }
        assert(ts_out == mt_out);

        Transitions ts_in = transition_system->get_incoming_transitions()[state_id];
        Transitions mt_in = match_tree->get_incoming_transitions(this->cartesian_sets, state);
        sort(ts_in.begin(), ts_in.end());
        sort(mt_in.begin(), mt_in.end());
        if (ts_in != mt_in) {
            cout << "State " << state_id << ", node: " << state.get_node_id() << endl;
            cout << "  TS in: " << ts_in << endl;
            cout << "  MT in: " << mt_in << endl;
        }
        assert(ts_in == mt_in);
    }
#endif

    return make_pair(v1_id, v2_id);
}

void Abstraction::print_statistics() const {
    cout << "States: " << get_num_states() << endl;
    cout << "Goal states: " << goals.size() << endl;
    if (transition_system) {
        transition_system->print_statistics();
    }
    if (match_tree) {
        match_tree->print_statistics();
    }
    cout << "Estimated memory usage for Cartesian states: "
         << get_num_states() * get_initial_state().get_cartesian_set().estimate_size_in_bytes() / 1024
         << " KB" << endl;
    refinement_hierarchy->print_statistics();
}
}

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
    : transition_system(utils::make_unique_ptr<TransitionSystem>(TaskProxy(*task).get_operators())),
      concrete_initial_state(TaskProxy(*task).get_initial_state()),
      goal_facts(task_properties::get_fact_pairs(TaskProxy(*task).get_goals())),
      refinement_hierarchy(utils::make_unique_ptr<RefinementHierarchy>(task)),
      debug(debug) {
    initialize_trivial_abstraction(get_domain_sizes(TaskProxy(*task)));
    match_tree = utils::make_unique_ptr<MatchTree>(
        TaskProxy(*task).get_operators(), *refinement_hierarchy, debug);
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

void Abstraction::mark_all_states_as_goals() {
    goals.clear();
    for (auto &state : states) {
        goals.insert(state->get_id());
    }
}

void Abstraction::initialize_trivial_abstraction(const vector<int> &domain_sizes) {
    unique_ptr<AbstractState> init_state =
        AbstractState::get_trivial_abstract_state(domain_sizes);
    init_id = init_state->get_id();
    goals.insert(init_state->get_id());
    states.push_back(move(init_state));
    cartesian_sets.push_back(utils::make_unique_ptr<CartesianSet>(domain_sizes));
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

    // TODO: store Cartesian sets only here and link to them from abstract states.
    this->cartesian_sets.resize(max(node_ids.first, node_ids.second) + 1);
    this->cartesian_sets[node_ids.first] = utils::make_unique_ptr<CartesianSet>(cartesian_sets.first);
    this->cartesian_sets[node_ids.second] = utils::make_unique_ptr<CartesianSet>(cartesian_sets.second);

    unique_ptr<AbstractState> v1 = utils::make_unique_ptr<AbstractState>(
        v1_id, node_ids.first, move(cartesian_sets.first));
    unique_ptr<AbstractState> v2 = utils::make_unique_ptr<AbstractState>(
        v2_id, node_ids.second, move(cartesian_sets.second));
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

    transition_system->rewire(states, v_id, *v1, *v2, var);
    match_tree->split(this->cartesian_sets, state, var);

    transition_system->dump();
    match_tree->dump();

    states.emplace_back();
    states[v1_id] = move(v1);
    states[v2_id] = move(v2);

    assert(init_id == 0);
    assert(get_initial_state().includes(concrete_initial_state));

    refinement_hierarchy->dump();

    for (int state_id = 0; state_id < get_num_states(); ++state_id) {
        const AbstractState &state = *states[state_id];
        cout << "State " << state_id << ", node: " << state.get_node_id() << endl;

        Transitions ts_out = transition_system->get_outgoing_transitions()[state_id];
        Operators ops_out = match_tree->get_outgoing_operators(state);
        Transitions mt_out = match_tree->get_outgoing_transitions(this->cartesian_sets, state);
        cout << "  TS out: " << ts_out << endl;
        cout << "  Operators out: " << ops_out << endl;
        cout << "  MT out: " << mt_out << endl;
        sort(ts_out.begin(), ts_out.end());
        sort(mt_out.begin(), mt_out.end());
        assert(ts_out == mt_out);

        Transitions ts_in = transition_system->get_incoming_transitions()[state_id];
        Operators ops_in = match_tree->get_incoming_operators(state);
        Transitions mt_in = match_tree->get_incoming_transitions(this->cartesian_sets, state);
        cout << "  TS in: " << ts_in << endl;
        cout << "  Operators in: " << ops_in << endl;
        cout << "  MT in: " << mt_in << endl;
        sort(ts_in.begin(), ts_in.end());
        sort(mt_in.begin(), mt_in.end());
        assert(ts_in == mt_in);
    }

    return make_pair(v1_id, v2_id);
}

void Abstraction::print_statistics() const {
    cout << "States: " << get_num_states() << endl;
    cout << "Goal states: " << goals.size() << endl;
    transition_system->print_statistics();
}
}

#include "abstraction.h"

#include "abstract_state.h"
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
Abstraction::Abstraction(const shared_ptr<AbstractTask> &task, utils::LogProxy &log)
    : transition_system(utils::make_unique_ptr<TransitionSystem>(TaskProxy(*task).get_operators())),
      concrete_initial_state(TaskProxy(*task).get_initial_state()),
      goal_facts(task_properties::get_fact_pairs(TaskProxy(*task).get_goals())),
      refinement_hierarchy(utils::make_unique_ptr<RefinementHierarchy>(task)),
      log(log) {
    initialize_trivial_abstraction(get_domain_sizes(TaskProxy(*task)));
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

int Abstraction::get_abstract_state_id(const State &state) const {
    int node_id = refinement_hierarchy->get_node_id(state);
    return refinement_hierarchy->nodes.at(node_id).get_state_id();
}

const TransitionSystem &Abstraction::get_transition_system() const {
    return *transition_system;
}

unique_ptr<RefinementHierarchy> Abstraction::extract_refinement_hierarchy() {
    assert(refinement_hierarchy);
    return move(refinement_hierarchy);
}

void Abstraction::mark_all_states_as_goals() {
    if (log.is_at_least_debug()) {
        log << "Mark all states as goals." << endl;
    }
    goals.clear();
    for (const auto &state : states) {
        goals.insert(state->get_id());
    }
}

void Abstraction::initialize_trivial_abstraction(const vector<int> &domain_sizes) {
    unique_ptr<AbstractState> init_state =
        AbstractState::get_trivial_abstract_state(domain_sizes);
    init_id = init_state->get_id();
    goals.insert(init_state->get_id());
    states.push_back(move(init_state));
}

pair<int, int> Abstraction::refine(
    const AbstractState &state, int var, const vector<int> &wanted) {
    if (log.is_at_least_debug())
        log << "Refine " << state << " for " << var << "=" << wanted << endl;

    int v_id = state.get_id();
    // Reuse state ID from obsolete parent to obtain consecutive IDs.
    int v1_id = v_id;
    int v2_id = get_num_states();

    pair<CartesianSet, CartesianSet> cartesian_sets =
        state.split_domain(var, wanted);
    CartesianSet &v1_cartesian_set = cartesian_sets.first;
    CartesianSet &v2_cartesian_set = cartesian_sets.second;

    vector<int> v2_values = wanted;
    assert(v2_values == v2_cartesian_set.get_values(var));
    // We partition the abstract domain into two subsets. Since the refinement
    // hierarchy stores helper nodes for all values of one of the children, we
    // prefer to use the smaller subset.
    if (v2_values.size() > 1) { // Quickly test necessary condition.
        vector<int> v1_values = v1_cartesian_set.get_values(var);
        if (v2_values.size() > v1_values.size()) {
            swap(v1_id, v2_id);
            swap(v1_values, v2_values);
            swap(v1_cartesian_set, v2_cartesian_set);
        }
    }

    // Ensure that the initial state always has state ID 0.
    if (v1_id == init_id &&
        v2_cartesian_set.test(var, concrete_initial_state[var].get_value())) {
        swap(v1_id, v2_id);
    }

    // Update refinement hierarchy.
    pair<NodeID, NodeID> node_ids = refinement_hierarchy->split(
        state.get_node_id(), var, v2_values, v1_id, v2_id);

    unique_ptr<AbstractState> v1 = utils::make_unique_ptr<AbstractState>(
        v1_id, node_ids.first, move(v1_cartesian_set));
    unique_ptr<AbstractState> v2 = utils::make_unique_ptr<AbstractState>(
        v2_id, node_ids.second, move(v2_cartesian_set));
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
        if (log.is_at_least_debug()) {
            log << "Goal states: " << goals.size() << endl;
        }
    }

    transition_system->rewire(states, v_id, *v1, *v2, var);

    states.emplace_back();
    states[v1_id] = move(v1);
    states[v2_id] = move(v2);

    assert(init_id == 0);
    assert(get_initial_state().includes(concrete_initial_state));

    return make_pair(v1_id, v2_id);
}

void Abstraction::print_statistics() const {
    if (log.is_at_least_normal()) {
        log << "States: " << get_num_states() << endl;
        log << "Goal states: " << goals.size() << endl;
        transition_system->print_statistics(log);
        log << "Nodes in refinement hierarchy: "
            << refinement_hierarchy->get_num_nodes() << endl;
    }
}
}

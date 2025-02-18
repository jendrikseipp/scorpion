#include "abstraction.h"

#include "abstract_state.h"
#include "match_tree.h"
#include "refinement_hierarchy.h"
#include "transition.h"
#include "transition_system.h"
#include "utils.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>
#include <execution>
#include <iostream>

using namespace std;

namespace cartesian_abstractions {
Abstraction::Abstraction(
    const shared_ptr<AbstractTask> &task,
    const shared_ptr<TransitionRewirer> &transition_rewirer,
    TransitionRepresentation transition_representation,
    utils::LogProxy &log)
    : transition_representation(transition_representation),
      transition_rewirer(transition_rewirer),
      concrete_initial_state(TaskProxy(*task).get_initial_state()),
      goal_facts(task_properties::get_fact_pairs(TaskProxy(*task).get_goals())),
      refinement_hierarchy(utils::make_unique_ptr<RefinementHierarchy>(task)),
      log(log),
      debug(log.is_at_least_debug()) {
    initialize_trivial_abstraction(get_domain_sizes(TaskProxy(*task)));

    if (transition_representation == TransitionRepresentation::STORE) {
        log << "Store transitions." << endl;
        transition_system = utils::make_unique_ptr<TransitionSystem>(*transition_rewirer);
    } else {
        log << "Create match tree." << endl;
        match_tree = utils::make_unique_ptr<MatchTree>(
            TaskProxy(*task).get_operators(),
            transition_rewirer->get_preconditions(),
            transition_rewirer->get_postconditions(),
            *refinement_hierarchy, cartesian_sets, debug);
    }
#ifndef NDEBUG
    if (!transition_system && debug) {
        transition_system = utils::make_unique_ptr<TransitionSystem>(*transition_rewirer);
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

const AbstractStates &Abstraction::get_states() const {
    return states;
}

int Abstraction::get_abstract_state_id(const State &state) const {
    return refinement_hierarchy->get_abstract_state_id(state);
}

unique_ptr<RefinementHierarchy> Abstraction::extract_refinement_hierarchy() {
    assert(refinement_hierarchy);
    return move(refinement_hierarchy);
}

const vector<FactPair> &Abstraction::get_preconditions(int op_id) const {
    return transition_rewirer->get_preconditions(op_id);
}

int Abstraction::get_num_operators() const {
    if (match_tree) {
        return match_tree->get_num_operators();
    }
    return transition_system->get_num_operators();
}

int Abstraction::get_num_stored_transitions() const {
    if (match_tree) {
        return 0;
    }
    return transition_system->get_num_non_loops();
}

Transitions Abstraction::get_incoming_transitions(int state_id) const {
    Transitions transitions = transition_system
        ? transition_system->get_incoming_transitions()[state_id]
        : match_tree->get_incoming_transitions(*states[state_id]);

    if (g_hacked_sort_transitions) {
        sort(execution::unseq, transitions.begin(), transitions.end());
    }
    return transitions;
}

Transitions Abstraction::get_outgoing_transitions(int state_id) const {
    Transitions transitions = transition_system
        ? transition_system->get_outgoing_transitions()[state_id]
        : match_tree->get_outgoing_transitions(*states[state_id]);

    if (g_hacked_sort_transitions) {
        sort(execution::unseq, transitions.begin(), transitions.end());
    }
    return transitions;
}

bool Abstraction::has_transition(int src, int op_id, int dest) const {
#ifdef NDEBUG
    ABORT("Abstraction::has_transition() should only be called in debug mode.");
#endif
    if (transition_system) {
        const Transitions &transitions = transition_system->get_outgoing_transitions()[src];
        return find(transitions.begin(), transitions.end(), Transition(op_id, dest)) != transitions.end();
    } else {
        bool valid = match_tree->has_transition(*states[src], op_id, *states[dest]);
#ifndef NDEBUG
        Transitions out = match_tree->get_outgoing_transitions(*states[src]);
        assert(count(out.begin(), out.end(), Transition(op_id, dest)) == static_cast<int>(valid));
#endif
        return valid;
    }
}

// TODO: ignore self-loops at unsolvable states.
vector<bool> Abstraction::get_looping_operators() const {
#ifndef NDEBUG
    if (match_tree && transition_system) {
        assert(match_tree->get_looping_operators(states) ==
               transition_system->get_looping_operators());
    }
#endif
    if (match_tree) {
        return match_tree->get_looping_operators(states);
    } else {
        assert(transition_system);
        return transition_system->get_looping_operators();
    }
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
    CartesianSet::set_static_members(domain_sizes);
    cartesian_sets.push_back(utils::make_unique_ptr<CartesianSet>(domain_sizes));
    unique_ptr<AbstractState> init_state =
        AbstractState::get_trivial_abstract_state(*cartesian_sets[0]);
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

    this->cartesian_sets.resize(max(node_ids.first, node_ids.second) + 1);
    this->cartesian_sets[node_ids.first] =
        utils::make_unique_ptr<CartesianSet>(move(v1_cartesian_set));
    this->cartesian_sets[node_ids.second] =
        utils::make_unique_ptr<CartesianSet>(move(v2_cartesian_set));

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
        if (log.is_at_least_debug()) {
            log << "Goal states: " << goals.size() << endl;
        }
    }

    if (transition_system) {
        transition_system->rewire(states, v_id, *v1, *v2, var);
    }

    states.emplace_back();
    states[v1_id] = move(v1);
    states[v2_id] = move(v2);

    assert(init_id == 0);
    assert(get_initial_state().includes(concrete_initial_state));

#ifndef NDEBUG
    if (match_tree && transition_system) {
        for (int state_id : {v1_id, v2_id}) {
            const AbstractState &state = *states[state_id];
            Transitions ts_out = transition_system->get_outgoing_transitions()[state_id];
            Transitions mt_out = match_tree->get_outgoing_transitions(state);
            sort(ts_out.begin(), ts_out.end());
            sort(mt_out.begin(), mt_out.end());
            if (ts_out != mt_out) {
                cout << "State " << state_id << ", node: " << state.get_node_id() << endl;
                cout << "  TS out: " << ts_out << endl;
                cout << "  MT out: " << mt_out << endl;
            }
            assert(ts_out == mt_out);

            Transitions ts_in = transition_system->get_incoming_transitions()[state_id];
            Transitions mt_in = match_tree->get_incoming_transitions(state);
            sort(ts_in.begin(), ts_in.end());
            sort(mt_in.begin(), mt_in.end());
            if (ts_in != mt_in) {
                cout << "State " << state_id << ", node: " << state.get_node_id() << endl;
                cout << "  TS in: " << ts_in << endl;
                cout << "  MT in: " << mt_in << endl;
            }
            assert(ts_in == mt_in);
        }
    }
#endif

    return {
        v1_id, v2_id
    };
}

void Abstraction::print_statistics() const {
    if (log.is_at_least_normal()) {
        log << "Cartesian states: " << get_num_states() << endl;
        log << "Cartesian goal states: " << goals.size() << endl;
        if (transition_system) {
            transition_system->print_statistics(log);
        }
        if (match_tree) {
            match_tree->print_statistics();
        }
        int num_helper_nodes = count(cartesian_sets.begin(), cartesian_sets.end(), nullptr);
        int num_cartesian_sets = cartesian_sets.size() - num_helper_nodes;
        log << "Cartesian helper nodes: " << num_helper_nodes << endl;
        log << "Cartesian sets: " << num_cartesian_sets << endl;
        refinement_hierarchy->print_statistics(log);
    }
}
}

#include "transition_system.h"

#include "abstract_state.h"
#include "transition.h"
#include "transition_rewirer.h"

#include "../utils/logging.h"

using namespace std;

namespace cartesian_abstractions {
TransitionSystem::TransitionSystem(const TransitionRewirer &rewirer)
    : rewirer(rewirer),
      num_non_loops(0),
      num_loops(0) {
    add_loops_in_trivial_abstraction();
}

void TransitionSystem::enlarge_vectors_by_one() {
    int new_num_states = get_num_states() + 1;
    outgoing.resize(new_num_states);
    incoming.resize(new_num_states);
    loops.resize(new_num_states);
}

void TransitionSystem::add_loops_in_trivial_abstraction() {
    assert(get_num_states() == 0);
    enlarge_vectors_by_one();
    int init_id = 0;
    for (int op_id = 0; op_id < get_num_operators(); ++op_id) {
        loops[init_id].push_back(op_id);
        ++num_loops;
    }
}

void TransitionSystem::rewire(
    const AbstractStates &states, int v_id,
    const AbstractState &v1, const AbstractState &v2, int var) {
    enlarge_vectors_by_one();

    num_non_loops -= (incoming[v_id].size() + outgoing[v_id].size());

    rewirer.rewire_transitions(incoming, outgoing, states, v_id, v1, v2, var);
    int v1_id = v1.get_id();
    int v2_id = v2.get_id();
    num_non_loops +=
        incoming[v1_id].size() + incoming[v2_id].size() +
        outgoing[v1_id].size() + outgoing[v2_id].size();

    int num_parent_loops = loops[v_id].size();
    rewirer.rewire_loops(loops, incoming, outgoing, v_id, v1, v2, var);
    int num_children_loops = loops[v1_id].size() + loops[v2_id].size();
    int num_transitions_between_children =
        count_if(outgoing[v1_id].begin(), outgoing[v1_id].end(),
                 [v2_id](const Transition &t) {return t.target_id == v2_id;}) +
        count_if(outgoing[v2_id].begin(), outgoing[v2_id].end(),
                 [v1_id](const Transition &t) {return t.target_id == v1_id;});

    num_non_loops += num_transitions_between_children;
    num_loops += num_children_loops - num_parent_loops;
}

const deque<Transitions> &TransitionSystem::get_incoming_transitions() const {
    return incoming;
}

const deque<Transitions> &TransitionSystem::get_outgoing_transitions() const {
    return outgoing;
}

vector<bool> TransitionSystem::get_looping_operators() const {
    vector<bool> operator_induces_self_loop(get_num_operators(), false);
    for (const auto &looping_ops : loops) {
        for (int op_id : looping_ops) {
            operator_induces_self_loop[op_id] = true;
        }
    }
    return operator_induces_self_loop;
}

const vector<FactPair> &TransitionSystem::get_preconditions(int op_id) const {
    return rewirer.get_preconditions(op_id);
}

int TransitionSystem::get_num_states() const {
    assert(incoming.size() == outgoing.size());
    assert(loops.size() == outgoing.size());
    return outgoing.size();
}

int TransitionSystem::get_num_operators() const {
    return rewirer.get_num_operators();
}

int TransitionSystem::get_num_non_loops() const {
    return num_non_loops;
}

int TransitionSystem::get_num_loops() const {
    return num_loops;
}

void TransitionSystem::print_statistics(utils::LogProxy &log) const {
    if (log.is_at_least_normal()) {
        int total_incoming_transitions = 0;
        utils::unused_variable(total_incoming_transitions);
        int total_outgoing_transitions = 0;
        int total_loops = 0;
        for (int state_id = 0; state_id < get_num_states(); ++state_id) {
            total_incoming_transitions += incoming[state_id].size();
            total_outgoing_transitions += outgoing[state_id].size();
            total_loops += loops[state_id].size();
        }
        assert(total_outgoing_transitions == total_incoming_transitions);
        assert(get_num_loops() == total_loops);
        assert(get_num_non_loops() == total_outgoing_transitions);
        log << "Looping transitions: " << total_loops << endl;
        log << "Non-looping transitions: " << total_outgoing_transitions << endl;
    }
}

void TransitionSystem::dump() const {
    for (int i = 0; i < get_num_states(); ++i) {
        cout << "State " << i << endl;
        cout << "  in: " << incoming[i] << endl;
        cout << "  out: " << outgoing[i] << endl;
        cout << "  loops: " << loops[i] << endl;
    }
}
}

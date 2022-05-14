#include "flaw_search.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "flaw.h"
#include "shortest_paths.h"
#include "transition_system.h"
#include "split_selector.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <iterator>

using namespace std;

namespace cegar {
CartesianSet FlawSearch::get_cartesian_set(const ConditionsProxy &conditions) const {
    CartesianSet cartesian_set(domain_sizes);
    for (FactProxy condition : conditions) {
        cartesian_set.set_single_value(condition.get_variable().get_id(),
                                       condition.get_value());
    }
    return cartesian_set;
}

int FlawSearch::get_abstract_state_id(const State &state) const {
    return abstraction.get_abstract_state_id(state);
}

Cost FlawSearch::get_h_value(int abstract_state_id) const {
    return shortest_paths.get_64bit_goal_distance(abstract_state_id);
}

OptimalTransitions FlawSearch::get_f_optimal_transitions(int abstract_state_id) const {
    OptimalTransitions transitions;
    for (const Transition &t :
         abstraction.get_transition_system().get_outgoing_transitions()[abstract_state_id]) {
        if (shortest_paths.is_optimal_transition(abstract_state_id, t.op_id, t.target_id)) {
            transitions[t.op_id].push_back(t.target_id);
        }
    }
    return transitions;
}

const vector<Transition> &FlawSearch::get_transitions(
    int abstract_state_id) const {
    return abstraction.get_transition_system().
           get_outgoing_transitions().at(abstract_state_id);
}

void FlawSearch::add_flaw(int abs_id, const State &state) {
    assert(abstraction.get_state(abs_id).includes(state));

    // Limit of concrete states we consider per abstract state.
    // If new abstract state (with potentially new h-value),
    // this if-statement is never true.
    if (flawed_states.num_concrete_states(abs_id) >=
        max_concrete_states_per_abstract_state) {
        return;
    }

    Cost h = get_h_value(abs_id);
    if (pick_flawed_abstract_state == PickFlawedAbstractState::MIN_H) {
        if (best_flaw_h > h) {
            flawed_states.clear();
        }
        if (best_flaw_h >= h) {
            best_flaw_h = h;
            flawed_states.add_state(abs_id, state, h);
        }
    } else if (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) {
        if (best_flaw_h < h) {
            flawed_states.clear();
        }
        if (best_flaw_h <= h) {
            best_flaw_h = h;
            flawed_states.add_state(abs_id, state, h);
        }
    } else {
        assert(pick_flawed_abstract_state == PickFlawedAbstractState::RANDOM
               || pick_flawed_abstract_state == PickFlawedAbstractState::FIRST
               || pick_flawed_abstract_state == PickFlawedAbstractState::BATCH_MIN_H);
        flawed_states.add_state(abs_id, state, h);
    }
}

void FlawSearch::initialize() {
    ++num_searches;
    last_refined_flawed_state = FlawedState::no_state;
    best_flaw_h = (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) ? 0 : INF_COSTS;
    assert(open_list.empty());
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    search_space = utils::make_unique_ptr<SearchSpace>(*state_registry, log);
    abstract_state_ids = utils::make_unique_ptr<PerStateInformation<int>>(MISSING);

    assert(flawed_states.empty());

    const State &initial_state = state_registry->get_initial_state();
    (*abstract_state_ids)[initial_state] = abstraction.get_initial_state().get_id();
    SearchNode node = search_space->get_node(initial_state);
    node.open_initial();
    open_list.push(initial_state.get_id());
}

SearchStatus FlawSearch::step() {
    if (open_list.empty()) {
        // Completely explored f-optimal state space.
        return FAILED;
    }
    StateID id = open_list.top();
    open_list.pop();
    State s = state_registry->lookup_state(id);
    SearchNode node = search_space->get_node(s);
    assert(!node.is_closed());
    node.close();
    assert(!node.is_dead_end());
    ++num_overall_expanded_concrete_states;

    if (task_properties::is_goal_state(task_proxy, s) &&
        pick_flawed_abstract_state != PickFlawedAbstractState::MAX_H) {
        return SOLVED;
    }

    bool found_flaw = false;
    int abs_id = (*abstract_state_ids)[s];
    assert(abs_id == get_abstract_state_id(s));

    // Check for each tr if the op is applicable or if there is a deviation
    for (auto &pair : get_f_optimal_transitions(abs_id)) {
        if (!utils::extra_memory_padding_is_reserved())
            return TIMEOUT;

        int op_id = pair.first;
        const vector<int> &targets = pair.second;

        OperatorProxy op = task_proxy.get_operators()[op_id];

        if (!task_properties::is_applicable(op, s)) {
            // Applicability flaw
            if (!found_flaw) {
                add_flaw(abs_id, s);
                found_flaw = true;
            }
            if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
                // Clear open list.
                stack<StateID>().swap(open_list);
                return FAILED;
            }
            continue;
        }

        State succ_state = state_registry->get_successor_state(s, op);
        SearchNode succ_node = search_space->get_node(succ_state);
        assert(!succ_node.is_dead_end());

        for (int target : targets) {
            if (!abstraction.get_state(target).includes(succ_state)) {
                // Deviation flaw
                if (!found_flaw) {
                    add_flaw(abs_id, s);
                    found_flaw = true;
                }
                if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
                    // Clear open list.
                    stack<StateID>().swap(open_list);
                    return FAILED;
                }
            } else if (succ_node.is_new()) {
                // No flaw
                (*abstract_state_ids)[succ_state] = target;
                succ_node.open(node, op, op.get_cost());
                open_list.push(succ_state.get_id());

                if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
                    // Only consider one successor.
                    break;
                }
            }
        }
        if (pick_flawed_abstract_state == PickFlawedAbstractState::FIRST) {
            // The node is necessarily new, since we consider only one successor and
            // follow f-optimal transitons
            // Only consider one successor.
            break;
        }
    }
    return IN_PROGRESS;
}

static void add_split(vector<vector<Split>> &splits, Split &&new_split) {
    vector<Split> &var_splits = splits[new_split.var_id];
    bool is_duplicate = false;
    for (auto &old_split : var_splits) {
        if (old_split == new_split) {
            is_duplicate = true;
            old_split.count += new_split.count;
        }
    }
    if (!is_duplicate) {
        var_splits.push_back(move(new_split));
    }
}

static vector<int> get_unaffected_variables(
    const OperatorProxy &op, int num_variables) {
    vector<bool> affected(num_variables);
    for (EffectProxy effect : op.get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        affected[fact.var] = true;
    }
    for (FactProxy precondition : op.get_preconditions()) {
        FactPair fact = precondition.get_pair();
        affected[fact.var] = true;
    }
    vector<int> unaffected_vars;
    unaffected_vars.reserve(num_variables);
    for (int var = 0; var < num_variables; ++var) {
        if (!affected[var]) {
            unaffected_vars.push_back(var);
        }
    }
    return unaffected_vars;
}

static void get_deviation_splits(
    const AbstractState &abs_state,
    const vector<State> &conc_states,
    const vector<int> &unaffected_variables,
    const AbstractState &target_abs_state,
    const vector<int> &domain_sizes,
    vector<vector<Split>> &splits) {
    /*
      For each fact in the concrete state that is not contained in the
      target abstract state, loop over all values in the domain of the
      corresponding variable. The values that are in both the current and
      the target abstract state are the "wanted" ones, i.e., the ones that
      we want to split off. This test can be specialized for precondition and
      deviation flaws.

      Let the desired abstract transition be (a, o, t) and the deviation (a, o,
      b). We distinguish three cases for each variable v:

      pre(o)[v] defined: no split possible since o is applicable in s.
      pre(o)[v] undefined, eff(o)[v] defined: no split possible since regression adds whole domain.
      pre(o)[v] and eff(o)[v] undefined: if s[v] \notin t[v], wanted = intersect(a[v], b[v]).
    */
    // Note: it could be faster to use an efficient hash map for this.
    vector<vector<int>> fact_count(domain_sizes.size());
    for (size_t var = 0; var < domain_sizes.size(); ++var) {
        fact_count[var].resize(domain_sizes[var], 0);
    }
    for (const State &conc_state : conc_states) {
        for (int var : unaffected_variables) {
            int state_value = conc_state[var].get_value();
            ++fact_count[var][state_value];
        }
    }
    for (size_t var = 0; var < domain_sizes.size(); ++var) {
        for (int value = 0; value < domain_sizes[var]; ++value) {
            if (fact_count[var][value] && !target_abs_state.contains(var, value)) {
                // Note: we could precompute the "wanted" vector, but not the split.
                vector<int> wanted;
                for (int value = 0; value < domain_sizes[var]; ++value) {
                    if (abs_state.contains(var, value) &&
                        target_abs_state.contains(var, value)) {
                        wanted.push_back(value);
                    }
                }
                assert(!wanted.empty());
                add_split(splits, Split(
                              abs_state.get_id(), var, value, move(wanted),
                              fact_count[var][value]));
            }
        }
    }
}

// TODO: Comment about split considering multiple transitions
unique_ptr<Split> FlawSearch::create_split(
    const vector<StateID> &state_ids, int abstract_state_id) {
    compute_splits_timer.resume();
    const AbstractState &abstract_state = abstraction.get_state(abstract_state_id);

    if (debug) {
        utils::g_log << endl;
        utils::g_log << "Create split for abstract state " << abstract_state_id << " and "
                     << state_ids.size() << " concrete states." << endl;
    }

    const TransitionSystem &ts = abstraction.get_transition_system();
    vector<vector<Split>> splits(task_proxy.get_variables().size());
    for (auto &pair : get_f_optimal_transitions(abstract_state_id)) {
        int op_id = pair.first;
        const vector<int> &targets = pair.second;
        OperatorProxy op = task_proxy.get_operators()[op_id];

        vector<State> states;
        states.reserve(state_ids.size());
        for (const StateID &state_id : state_ids) {
            states.push_back(state_registry->lookup_state(state_id));
            assert(abstract_state.includes(states.back()));
        }

        vector<bool> applicable(states.size(), true);
        for (FactPair fact : ts.get_preconditions(op_id)) {
            vector<int> state_value_count(domain_sizes[fact.var], 0);
            for (size_t i = 0; i < states.size(); ++i) {
                const State &state = states[i];
                int state_value = state[fact.var].get_value();
                if (state_value != fact.value) {
                    // Applicability flaw
                    applicable[i] = false;
                    ++state_value_count[state_value];
                }
            }
            for (int value = 0; value < domain_sizes[fact.var]; ++value) {
                if (state_value_count[value] > 0) {
                    assert(value != fact.value);
                    add_split(splits, Split(
                                  abstract_state_id, fact.var, value,
                                  {fact.value}, state_value_count[value]));
                }
            }
        }

        phmap::flat_hash_map<int, vector<State>> deviation_states_by_target;
        for (size_t i = 0; i < states.size(); ++i) {
            if (!applicable[i]) {
                continue;
            }
            const State &state = states[i];
            assert(task_properties::is_applicable(op, state));
            State succ_state = state_registry->get_successor_state(state, op);
            bool target_hit = false;
            for (int target : targets) {
                if (!utils::extra_memory_padding_is_reserved()) {
                    return nullptr;
                }

                // At most one of the f-optimal targets can include the successor state.
                if (!target_hit && abstraction.get_state(target).includes(succ_state)) {
                    // No flaw
                    target_hit = true;
                } else {
                    // Deviation flaw
                    assert(target != get_abstract_state_id(succ_state));
                    deviation_states_by_target[target].push_back(state);
                }
            }
        }

        for (auto &pair : deviation_states_by_target) {
            int target = pair.first;
            const vector<State> &deviation_states = pair.second;
            if (!deviation_states.empty()) {
                int num_vars = domain_sizes.size();
                get_deviation_splits(
                    abstract_state, deviation_states,
                    get_unaffected_variables(op, num_vars),
                    abstraction.get_state(target), domain_sizes, splits);
            }
        }
    }

    int num_splits = 0;
    for (auto &var_splits : splits) {
        num_splits += var_splits.size();
    }
    if (debug) {
        utils::g_log << "Unique splits: " << num_splits << endl;
    }
    compute_splits_timer.stop();

    if (num_splits == 0) {
        return nullptr;
    }

    pick_split_timer.resume();
    Split split = split_selector.pick_split(abstract_state, move(splits), rng);
    pick_split_timer.stop();
    return utils::make_unique_ptr<Split>(move(split));
}

SearchStatus FlawSearch::search_for_flaws(const utils::CountdownTimer &cegar_timer) {
    flaw_search_timer.resume();
    if (debug) {
        utils::g_log << "Search for flaws" << endl;
    }
    initialize();
    int before_expanded_states = num_overall_expanded_concrete_states;
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        if (cegar_timer.is_expired()) {
            search_status = TIMEOUT;
            // Clear open list.
            stack<StateID>().swap(open_list);
            break;
        }

        // Max Expansions reached
        int cur_expanded_states = num_overall_expanded_concrete_states -
            before_expanded_states;
        if (cur_expanded_states >= max_state_expansions) {
            // No flaw found
            if (flawed_states.num_abstract_states() == 0) {
                utils::release_extra_memory_padding();
                utils::g_log << "Max expansions reached with no flaw!" << endl;
                search_status = TIMEOUT;
            } else {
                utils::g_log << "Max expansions reached with flaws!" << endl;
                search_status = FAILED;
            }
            stack<StateID>().swap(open_list);
            break;
        }
        search_status = step();
    }

    max_expanded_concrete_states = max(max_expanded_concrete_states,
                                       num_overall_expanded_concrete_states -
                                       before_expanded_states);
    if (debug) {
        utils::g_log << "Flaw search expanded "
                     << num_overall_expanded_concrete_states - before_expanded_states
                     << " states" << endl;
    }

    // Check if MAX_H_SINGLE did not find a single flaw
    if (pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H && search_status == FAILED
        && flawed_states.num_abstract_states() == 0 && open_list.empty()) {
        search_status = SOLVED;
    }

    flaw_search_timer.stop();
    return search_status;
}

unique_ptr<Split> FlawSearch::get_single_split(const utils::CountdownTimer &cegar_timer) {
    auto search_status = search_for_flaws(cegar_timer);

    // Memory padding
    if (search_status == TIMEOUT)
        return nullptr;

    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        FlawedState flawed_state = flawed_states.pop_random_flawed_state_and_clear(rng);
        StateID state_id = *rng.choose(flawed_state.concrete_states);

        if (debug) {
            vector<OperatorID> trace;
            search_space->trace_path(state_registry->lookup_state(state_id), trace);
            vector<string> operator_names;
            operator_names.reserve(trace.size());
            for (OperatorID op_id : trace) {
                operator_names.push_back(task_proxy.get_operators()[op_id].get_name());
            }
            utils::g_log << "Path (without last operator): " << operator_names << endl;
        }

        return create_split({state_id}, flawed_state.abs_id);
    }
    assert(search_status == SOLVED);
    return nullptr;
}

FlawedState FlawSearch::get_flawed_state_with_min_h() {
    while (!flawed_states.empty()) {
        FlawedState flawed_state = flawed_states.pop_flawed_state_with_min_h();
        Cost old_h = flawed_state.h;
        int abs_id = flawed_state.abs_id;
        assert(get_h_value(abs_id) >= old_h);
        if (get_h_value(abs_id) == old_h) {
            if (debug) {
                utils::g_log << "Reuse flawed state: " << abs_id << endl;
            }
            return flawed_state;
        } else {
            if (debug) {
                utils::g_log << "Ignore flawed state with increased f value: " << abs_id << endl;
            }
        }
    }
    // The f value increased for all states.
    return FlawedState::no_state;
}


unique_ptr<Split>
FlawSearch::get_min_h_batch_split(const utils::CountdownTimer &cegar_timer) {
    assert(pick_flawed_abstract_state == PickFlawedAbstractState::BATCH_MIN_H);
    if (last_refined_flawed_state != FlawedState::no_state) {
        // Handle flaws of the last refined abstract state.
        Cost old_h = last_refined_flawed_state.h;
        for (const StateID &state_id : last_refined_flawed_state.concrete_states) {
            State state = state_registry->lookup_state(state_id);
            // We only add non-goal states to flawed_states.
            assert(!task_properties::is_goal_state(task_proxy, state));
            int abs_id = get_abstract_state_id(state);
            if (get_h_value(abs_id) == old_h) {
                add_flaw(abs_id, state);
            }
        }
    }

    FlawedState flawed_state = get_flawed_state_with_min_h();
    auto search_status = SearchStatus::FAILED;
    if (flawed_state == FlawedState::no_state) {
        search_status = search_for_flaws(cegar_timer);
        flawed_states.dump();
        if (search_status == SearchStatus::FAILED) {
            flawed_state = get_flawed_state_with_min_h();
        }
    }

    if (debug) {
        utils::g_log << "Use flawed state: " << flawed_state << " with h=" << flawed_state.h << endl;
    }

    // Memory padding
    if (search_status == TIMEOUT)
        return nullptr;

    if (search_status == FAILED) {
        // There are flaws to refine.
        assert(flawed_state != FlawedState::no_state);

        unique_ptr<Split> split;
        split = create_split(flawed_state.concrete_states, flawed_state.abs_id);

        if (!utils::extra_memory_padding_is_reserved()) {
            return nullptr;
        }

        if (split) {
            last_refined_flawed_state = move(flawed_state);
        } else {
            last_refined_flawed_state = FlawedState::no_state;
            // We selected an abstract state without any flaws, so we try again.
            return get_min_h_batch_split(cegar_timer);
        }

        return split;
    }

    assert(search_status == SOLVED);
    return nullptr;
}

FlawSearch::FlawSearch(
    const shared_ptr<AbstractTask> &task,
    const vector<int> &domain_sizes,
    const Abstraction &abstraction,
    const ShortestPaths &shortest_paths,
    utils::RandomNumberGenerator &rng,
    PickFlawedAbstractState pick_flawed_abstract_state,
    PickSplit pick_split,
    PickSplit tiebreak_split,
    int max_concrete_states_per_abstract_state,
    int max_state_expansions,
    bool debug) :
    task_proxy(*task),
    log(utils::get_silent_log()),
    domain_sizes(domain_sizes),
    abstraction(abstraction),
    shortest_paths(shortest_paths),
    split_selector(task, pick_split, tiebreak_split, debug),
    rng(rng),
    pick_flawed_abstract_state(pick_flawed_abstract_state),
    max_concrete_states_per_abstract_state(max_concrete_states_per_abstract_state),
    max_state_expansions(max_state_expansions),
    debug(debug),
    last_refined_flawed_state(FlawedState::no_state),
    best_flaw_h((pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H) ? 0 : INF),
    num_searches(0),
    num_overall_expanded_concrete_states(0),
    max_expanded_concrete_states(0),
    flaw_search_timer(false),
    compute_splits_timer(false),
    pick_split_timer(false) {
}

unique_ptr<Split> FlawSearch::get_split(const utils::CountdownTimer &cegar_timer) {
    unique_ptr<Split> split;

    switch (pick_flawed_abstract_state) {
    case PickFlawedAbstractState::FIRST:
    case PickFlawedAbstractState::RANDOM:
    case PickFlawedAbstractState::MIN_H:
    case PickFlawedAbstractState::MAX_H:
        split = get_single_split(cegar_timer);
        break;
    case PickFlawedAbstractState::BATCH_MIN_H:
        split = get_min_h_batch_split(cegar_timer);
        break;
    default:
        utils::g_log << "Invalid pick flaw strategy: "
                     << static_cast<int>(pick_flawed_abstract_state)
                     << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }

    if (split) {
        assert(!(pick_flawed_abstract_state == PickFlawedAbstractState::MAX_H
                 || pick_flawed_abstract_state == PickFlawedAbstractState::MIN_H)
               || best_flaw_h == get_h_value(split->abstract_state_id));
    }
    return split;
}

unique_ptr<Split> FlawSearch::get_split_legacy(const Solution &solution) {
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    if (debug)
        utils::g_log << "Check solution:" << endl;

    const AbstractState *abstract_state = &abstraction.get_initial_state();
    State concrete_state = state_registry->get_initial_state();
    assert(abstract_state->includes(concrete_state));

    if (debug)
        utils::g_log << "  Initial abstract state: " << *abstract_state << endl;

    for (const Transition &step : solution) {
        OperatorProxy op = task_proxy.get_operators()[step.op_id];
        const AbstractState *next_abstract_state = &abstraction.get_state(step.target_id);
        if (task_properties::is_applicable(op, concrete_state)) {
            if (debug)
                utils::g_log << "  Move to " << *next_abstract_state << " with "
                             << op.get_name() << endl;
            State next_concrete_state = state_registry->get_successor_state(concrete_state, op);
            if (!next_abstract_state->includes(next_concrete_state)) {
                if (debug)
                    utils::g_log << "  Paths deviate." << endl;
                return create_split({concrete_state.get_id()}, abstract_state->get_id());
            }
            abstract_state = next_abstract_state;
            concrete_state = move(next_concrete_state);
        } else {
            if (debug)
                utils::g_log << "  Operator not applicable: " << op.get_name() << endl;
            return create_split({concrete_state.get_id()}, abstract_state->get_id());
        }
    }
    assert(abstraction.get_goals().count(abstract_state->get_id()));
    if (task_properties::is_goal_state(task_proxy, concrete_state)) {
        // We found a concrete solution.
        return nullptr;
    } else {
        if (debug)
            utils::g_log << "  Goal test failed." << endl;
        return create_split({concrete_state.get_id()}, abstract_state->get_id());
    }
}

void FlawSearch::print_statistics() const {
    // Avoid division by zero for corner cases.
    int flaws = max(1, abstraction.get_num_states() - 1);
    int searches = max(0ul, num_searches);
    int expansions = num_overall_expanded_concrete_states;
    utils::g_log << endl;
    utils::g_log << "#Flaw searches: " << searches << endl;
    utils::g_log << "#Flaws refined: " << flaws << endl;
    utils::g_log << "#Expanded concrete states: " << expansions << endl;
    utils::g_log << "#Max expanded concrete states in one flaw search: "
                 << max_expanded_concrete_states << endl;
    utils::g_log << "Flaw search time: " << flaw_search_timer << endl;
    utils::g_log << "Time for computing splits: " << compute_splits_timer << endl;
    utils::g_log << "Time for selecting splits: " << pick_split_timer << endl;
    utils::g_log << "Avg flaws refined: "
                 << flaws / static_cast<float>(searches) << endl;
    utils::g_log << "Avg expanded concrete states: "
                 << expansions / static_cast<float>(searches) << endl;
    utils::g_log << "Avg Flaw search time: " << flaw_search_timer() / searches << endl;
    utils::g_log << endl;
}
}

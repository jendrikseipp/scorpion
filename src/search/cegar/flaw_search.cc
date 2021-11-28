#include "flaw_search.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "flaw.h"
#include "transition_system.h"
#include "shortest_paths.h"
#include "split_selector.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
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

int FlawSearch::get_h_value(int abstract_state_id) const {
    return shortest_paths.get_goal_distance(abstract_state_id);
}

bool FlawSearch::is_f_optimal_transition(int abstract_state_id,
                                         const Transition &tr) const {
    int source_h_value = get_h_value(abstract_state_id);
    int target_h_value = get_h_value(tr.target_id);
    int op_cost = task_proxy.get_operators()[tr.op_id].get_cost();
    return source_h_value - op_cost == target_h_value;
}

const vector<Transition> &FlawSearch::get_transitions(
    int abstract_state_id) const {
    return abstraction.get_transition_system().
           get_outgoing_transitions().at(abstract_state_id);
}

void FlawSearch::add_flaw(int abs_id, const State &state) {
    // Using a reference to flawed_states[abs_id] doesn't work since it creates a temporary.
    assert(find(flawed_states[abs_id].begin(), flawed_states[abs_id].end(), state) ==
           flawed_states[abs_id].end());
    int h = get_h_value(abs_id);
    if (pick_flaw == PickFlaw::MIN_H_SINGLE
        || pick_flaw == PickFlaw::MIN_H_BATCH
        || pick_flaw == PickFlaw::MIN_H_BATCH_MULTI_SPLIT) {
        if (best_flaw_h > h) {
            flawed_states.clear();
        }
        if (best_flaw_h >= h) {
            best_flaw_h = h;
            flawed_states[abs_id].push_back(state);
        }
    } else if (pick_flaw == PickFlaw::MAX_H_SINGLE) {
        if (best_flaw_h < h) {
            flawed_states.clear();
        }
        if (best_flaw_h <= h) {
            best_flaw_h = h;
            flawed_states[abs_id].push_back(state);
        }
    } else {
        assert(pick_flaw == PickFlaw::RANDOM_H_SINGLE);
        flawed_states[abs_id].push_back(state);
    }
    assert(!flawed_states.empty());
}

void FlawSearch::initialize() {
    ++num_searches;
    last_refined_abstract_state_id = -1;
    best_flaw_h = (pick_flaw == PickFlaw::MAX_H_SINGLE) ? -INF : INF;
    assert(open_list.empty());
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    search_space = utils::make_unique_ptr<SearchSpace>(*state_registry);
    statistics = utils::make_unique_ptr<SearchStatistics>(utils::Verbosity::SILENT);

    flawed_states.clear();

    const State &initial_state = state_registry->get_initial_state();
    SearchNode node = search_space->get_node(initial_state);
    node.open_initial();
    open_list.push(initial_state.get_id());
}

SearchStatus FlawSearch::step() {
    if (open_list.empty()) {
        // Completely explored f-optimal state space.
        return FAILED;
    }
    StateID id = open_list.front();
    open_list.pop();
    State s = state_registry->lookup_state(id);
    SearchNode node = search_space->get_node(s);

    assert(!node.is_closed());
    assert(node.get_real_g() + get_h_value(get_abstract_state_id(s))
           <= get_h_value(abstraction.get_initial_state().get_id()));

    node.close();
    assert(!node.is_dead_end());
    ++num_overall_expanded_concrete_states;
    statistics->inc_expanded();

    if (task_properties::is_goal_state(task_proxy, s)) {
        return SOLVED;
    }

    bool found_flaw = false;
    int abs_id = get_abstract_state_id(s);

    // Check for each tr if the op is applicable or if there is a deviation
    for (const Transition &tr : get_transitions(abs_id)) {
        if (!utils::extra_memory_padding_is_reserved())
            return TIMEOUT;

        if (is_f_optimal_transition(abs_id, tr)) {
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];

            // Applicability flaw
            if (!task_properties::is_applicable(op, s)) {
                if (!found_flaw) {
                    add_flaw(abs_id, s);
                    found_flaw = true;
                }
                if (pick_flaw == PickFlaw::MAX_H_SINGLE) {
                    return FAILED;
                }
                continue;
            }
            State succ_state = state_registry->get_successor_state(s, op);
            // Deviation flaw
            if (!abstraction.get_state(tr.target_id).includes(succ_state)) {
                if (!found_flaw) {
                    add_flaw(abs_id, s);
                    found_flaw = true;
                }
                if (pick_flaw == PickFlaw::MAX_H_SINGLE) {
                    return FAILED;
                }
                continue;
            }

            statistics->inc_generated();
            SearchNode succ_node = search_space->get_node(succ_state);
            assert(!succ_node.is_dead_end());

            if (succ_node.is_new()) {
                succ_node.open(node, op, op.get_cost());
                statistics->inc_evaluated_states();
                open_list.push(succ_state.get_id());
            }
        }
    }
    return IN_PROGRESS;
}

static void get_precondition_splits(
    const AbstractState &abs_state,
    const State &conc_state,
    const ConditionsProxy &preconditions,
    vector<Split> &splits) {
    for (FactProxy precondition_proxy : preconditions) {
        FactPair fact = precondition_proxy.get_pair();
        assert(abs_state.contains(fact.var, fact.value));
        int state_value = conc_state[fact.var].get_value();
        if (state_value != fact.value) {
            vector<int> wanted = {fact.value};
            splits.emplace_back(abs_state.get_id(), fact.var, state_value, move(wanted));
        }
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
    const State &conc_state,
    const vector<int> &unaffected_variables,
    const AbstractState &target_abs_state,
    const vector<int> &domain_sizes,
    vector<Split> &splits) {
    /*
      Let the abstract transition be (a, o, b). We distinguish three cases for
      each variable v:

      pre(o)[v] defined: no split possible since o is applicable in s.
      pre(o)[v] undefined, eff(o)[v] defined: no split possible since regression adds whole domain.
      pre(o)[v] and eff(o)[v] undefined: if s[v] \notin target[v], wanted = intersect(a[v], b[v]).
    */
    for (int var : unaffected_variables) {
        int state_value = conc_state[var].get_value();
        if (!target_abs_state.contains(var, state_value)) {
            vector<int> wanted;
            for (int value = 0; value < domain_sizes[var]; ++value) {
                if (abs_state.contains(var, value) &&
                    target_abs_state.contains(var, value)) {
                    wanted.push_back(value);
                }
            }
            assert(!wanted.empty());
            splits.emplace_back(abs_state.get_id(), var, state_value, move(wanted));
        }
    }
}

unique_ptr<Split> FlawSearch::create_split(
    const vector<State> &states, int abstract_state_id) {
    const AbstractState &abstract_state = abstraction.get_state(abstract_state_id);

    vector<Split> splits;
    for (const Transition &tr : get_transitions(abstract_state_id)) {
        if (is_f_optimal_transition(abstract_state_id, tr)) {
            OperatorProxy op = task_proxy.get_operators()[tr.op_id];
            int num_vars = domain_sizes.size();
            vector<int> unaffected_variables = get_unaffected_variables(op, num_vars);

            for (const State &state : states) {
                // Applicability flaw
                if (!task_properties::is_applicable(op, state)) {
                    get_precondition_splits(
                        abstract_state, state, op.get_preconditions(), splits);
                } else {
                    // Flaws are only guaranteed to exist for fringe states.
                    if ((pick_flaw == PickFlaw::MAX_H_SINGLE ||
                         pick_flaw == PickFlaw::RANDOM_H_SINGLE)
                        && abstraction.get_state(tr.target_id).includes(
                            state_registry->get_successor_state(state, op))) {
                        continue;
                    }

                    // Deviation flaw
                    assert(tr.target_id != get_abstract_state_id(
                               state_registry->get_successor_state(state, op)));
                    const AbstractState &target_abstract_state =
                        abstraction.get_state(tr.target_id);
                    get_deviation_splits(
                        abstract_state, state, unaffected_variables,
                        target_abstract_state, domain_sizes, splits);
                }
            }
        }
    }

    return split_selector.pick_split(abstract_state, move(splits), rng);
}

SearchStatus FlawSearch::search_for_flaws() {
    initialize();
    size_t cur_expanded_states = num_overall_expanded_concrete_states;
    SearchStatus search_status = IN_PROGRESS;
    while (search_status == IN_PROGRESS) {
        search_status = step();
    }

    if (debug) {
        cout << endl;
        utils::g_log << "Expanded "
                     << num_overall_expanded_concrete_states - cur_expanded_states
                     << " states." << endl;
        utils::g_log << "Flawed States: " << endl;
        if (search_status == FAILED) {
            for (auto const &pair : flawed_states) {
                for (const State &s : pair.second) {
                    utils::g_log << "  <" << pair.first << "," << s.get_id()
                                 << ">: " << *create_split({s}, pair.first)
                                 << endl;
                }
            }
        }
    }
    return search_status;
}

unique_ptr<Split> FlawSearch::get_single_split() {
    auto search_status = search_for_flaws();

    // Memory padding
    if (search_status == TIMEOUT)
        return nullptr;

    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        auto random_bucket = next(flawed_states.begin(), rng(flawed_states.size()));
        int abstract_state_id = random_bucket->first;
        const State &state = *rng.choose(random_bucket->second);

        if (debug) {
            vector<OperatorID> trace;
            search_space->trace_path(state, trace);
            vector<string> operator_names;
            operator_names.reserve(trace.size());
            for (OperatorID op_id : trace) {
                operator_names.push_back(task_proxy.get_operators()[op_id].get_name());
            }
            utils::g_log << "Path (without last operator): " << operator_names << endl;
        }

        return create_split({state}, abstract_state_id);
    }
    assert(search_status == SOLVED);
    return nullptr;
}

unique_ptr<Split>
FlawSearch::get_min_h_batch_split() {
    // Handle flaws of refined abstract state
    if (last_refined_abstract_state_id != -1) {
        vector<State> states_to_handle =
            move(flawed_states.at(last_refined_abstract_state_id));
        flawed_states.erase(last_refined_abstract_state_id);
        for (const State &s : states_to_handle) {
            // We only add non-goal states to flawed_states.
            assert(!task_properties::is_goal_state(task_proxy, s));
            int abs_id = get_abstract_state_id(s);
            if (get_h_value(abs_id) == best_flaw_h) {
                add_flaw(abs_id, s);
            }
        }
    }

    auto search_status = SearchStatus::FAILED;
    if (flawed_states.empty()) {
        search_status = search_for_flaws();
    }

    // Memory padding
    if (search_status == TIMEOUT)
        return nullptr;

    // There are flaws to refine.
    if (search_status == FAILED) {
        assert(!flawed_states.empty());

        /* It doesn't matter in which order we consider the abstract states with
           minimal h value since we'll refine all of them anyway. */
        auto first_bucket = flawed_states.begin();
        int abstract_state_id = first_bucket->first;

        unique_ptr<Split> split;
        if (pick_flaw == PickFlaw::MIN_H_BATCH_MULTI_SPLIT) {
            split = create_split(first_bucket->second, abstract_state_id);
        } else {
            const State &state = *rng.choose(first_bucket->second);
            split = create_split({state}, abstract_state_id);
        }

        return split;
    }

    assert(search_status == SOLVED);
    return nullptr;
}

FlawSearch::FlawSearch(const shared_ptr<AbstractTask> &task,
                       const vector<int> &domain_sizes,
                       const Abstraction &abstraction,
                       const ShortestPaths &shortest_paths,
                       utils::RandomNumberGenerator &rng,
                       PickFlaw pick_flaw,
                       PickSplit pick_split,
                       bool debug) :
    task_proxy(*task),
    domain_sizes(domain_sizes),
    abstraction(abstraction),
    shortest_paths(shortest_paths),
    split_selector(task, pick_split, debug),
    rng(rng),
    pick_flaw(pick_flaw),
    debug(debug),
    last_refined_abstract_state_id(-1),
    best_flaw_h((pick_flaw == PickFlaw::MAX_H_SINGLE) ? -INF : INF),
    num_searches(0),
    num_overall_expanded_concrete_states(0) {
    timer.stop();
    timer.reset();
}

unique_ptr<Split> FlawSearch::get_split() {
    timer.resume();
    unique_ptr<Split> split = nullptr;

    switch (pick_flaw) {
    case PickFlaw::RANDOM_H_SINGLE:
    case PickFlaw::MIN_H_SINGLE:
    case PickFlaw::MAX_H_SINGLE:
        split = get_single_split();
        break;
    case PickFlaw::MIN_H_BATCH:
    case PickFlaw::MIN_H_BATCH_MULTI_SPLIT:
        split = get_min_h_batch_split();
        break;
    default:
        utils::g_log << "Invalid pick flaw strategy: " << static_cast<int>(pick_flaw)
                     << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }

    if (split) {
        last_refined_abstract_state_id = split->abstract_state_id;
        assert(pick_flaw == PickFlaw::RANDOM_H_SINGLE
               || best_flaw_h == get_h_value(split->abstract_state_id));
    }
    timer.stop();
    return split;
}

void FlawSearch::print_statistics() const {
    // Avoid division by zero for corner cases.
    float num_overall_refined_flaws = max(1, abstraction.get_num_states() - 1);
    float searches = static_cast<float>(max(1ul, num_searches));
    utils::g_log << endl;
    utils::g_log << "#Flaw searches: " << searches << endl;
    utils::g_log << "#Flaws refined: " << num_overall_refined_flaws << endl;
    utils::g_log << "#Expanded concrete states: "
                 << num_overall_expanded_concrete_states << endl;
    utils::g_log << "Flaw search time: " << timer << endl;
    utils::g_log << "Avg flaws refined: "
                 << num_overall_refined_flaws / searches << endl;
    utils::g_log << "Avg expanded concrete states: "
                 << num_overall_expanded_concrete_states / searches << endl;
    utils::g_log << "Avg Flaw search time: " << timer() / searches << endl;
    utils::g_log << endl;
}
}

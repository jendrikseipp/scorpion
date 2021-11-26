#include "split_selector.h"

#include "abstract_state.h"
#include "utils.h"
#include "flaw.h"

#include "../heuristics/additive_heuristic.h"

#include "../utils/memory.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace std;

namespace cegar {
SplitSelector::SplitSelector(
    const shared_ptr<AbstractTask> &task,
    PickSplit pick,
    bool debug)
    : task(task),
      task_proxy(*task),
      debug(debug),
      pick(pick) {
    if (pick == PickSplit::MIN_HADD || pick == PickSplit::MAX_HADD) {
        additive_heuristic = create_additive_heuristic(task);
        additive_heuristic->compute_heuristic_for_cegar(
            task_proxy.get_initial_state());
    }
}

// Define here to avoid include in header.
SplitSelector::~SplitSelector() {
}

int SplitSelector::get_num_unwanted_values(
    const AbstractState &state, const Split &split) const {
    int num_unwanted_values = state.count(split.var_id) - split.values.size();
    assert(num_unwanted_values >= 1);
    return num_unwanted_values;
}

double SplitSelector::get_refinedness(const AbstractState &state, int var_id) const {
    double all_values = task_proxy.get_variables()[var_id].get_domain_size();
    assert(all_values >= 2);
    double remaining_values = state.count(var_id);
    assert(2 <= remaining_values && remaining_values <= all_values);
    double refinedness = -(remaining_values / all_values);
    assert(-1.0 <= refinedness && refinedness < 0.0);
    return refinedness;
}

int SplitSelector::get_hadd_value(int var_id, int value) const {
    assert(additive_heuristic);
    int hadd = additive_heuristic->get_cost_for_cegar(var_id, value);
    assert(hadd != -1);
    return hadd;
}

int SplitSelector::get_min_hadd_value(int var_id, const vector<int> &values) const {
    int min_hadd = numeric_limits<int>::max();
    for (int value : values) {
        const int hadd = get_hadd_value(var_id, value);
        if (hadd < min_hadd) {
            min_hadd = hadd;
        }
    }
    return min_hadd;
}

int SplitSelector::get_max_hadd_value(int var_id, const vector<int> &values) const {
    int max_hadd = -1;
    for (int value : values) {
        const int hadd = get_hadd_value(var_id, value);
        if (hadd > max_hadd) {
            max_hadd = hadd;
        }
    }
    return max_hadd;
}

void SplitSelector::get_possible_splits(
    const AbstractState &abstract_state,
    const State &concrete_state,
    const CartesianSet &desired_cartesian_set,
    vector<Split> &splits) const {
    /*
      For each fact in the concrete state that is not contained in the
      desired abstract state, loop over all values in the domain of the
      corresponding variable. The values that are in both the current and
      the desired abstract state are the "wanted" ones, i.e., the ones that
      we want to split off.
    */
    for (FactProxy wanted_fact_proxy : concrete_state) {
        FactPair fact = wanted_fact_proxy.get_pair();
        if (!desired_cartesian_set.test(fact.var, fact.value)) {
            VariableProxy var = wanted_fact_proxy.get_variable();
            int var_id = var.get_id();
            vector<int> wanted;
            for (int value = 0; value < var.get_domain_size(); ++value) {
                if (abstract_state.contains(var_id, value) &&
                    desired_cartesian_set.test(var_id, value)) {
                    wanted.push_back(value);
                }
            }
            assert(!wanted.empty());
            splits.emplace_back(var_id, fact.value, move(wanted));
        }
    }
    assert(!splits.empty());
}

double SplitSelector::rate_split(const AbstractState &state, const Split &split) const {
    int var_id = split.var_id;
    const vector<int> &values = split.values;
    double rating;
    switch (pick) {
    case PickSplit::MIN_UNWANTED:
        rating = -get_num_unwanted_values(state, split);
        break;
    case PickSplit::MAX_UNWANTED:
        rating = get_num_unwanted_values(state, split);
        break;
    case PickSplit::MIN_REFINED:
        rating = -get_refinedness(state, var_id);
        break;
    case PickSplit::MAX_REFINED:
        rating = get_refinedness(state, var_id);
        break;
    case PickSplit::MIN_HADD:
        rating = -get_min_hadd_value(var_id, values);
        break;
    case PickSplit::MAX_HADD:
        rating = get_max_hadd_value(var_id, values);
        break;
    case PickSplit::MAX_COVER:
        rating = get_refinedness(state, var_id);
        break;
    default:
        utils::g_log << "Invalid pick strategy: " << static_cast<int>(pick) << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    return rating;
}

unique_ptr<Flaw> SplitSelector::pick_split(
    const AbstractState &abstract_state,
    const State &concrete_state,
    const CartesianSet &desired_cartesian_set,
    utils::RandomNumberGenerator &rng) const {
    vector<Split> splits;
    get_possible_splits(abstract_state, concrete_state,
                        desired_cartesian_set, splits);
    assert(!splits.empty());

    if (splits.size() == 1) {
        return utils::make_unique_ptr<Flaw>(
            abstract_state.get_id(), move(splits[0]));
    }

    if (pick == PickSplit::RANDOM) {
        return utils::make_unique_ptr<Flaw>(
            abstract_state.get_id(),
            move(*rng.choose(splits)));
    }

    double max_rating = numeric_limits<double>::lowest();
    Split *selected_split = nullptr;
    for (Split &split : splits) {
        double rating = rate_split(abstract_state, split);
        if (rating > max_rating) {
            selected_split = &split;
            max_rating = rating;
        }
    }
    // utils::g_log << "SELECTED: " << *selected_split << endl;
    assert(selected_split);
    return utils::make_unique_ptr<Flaw>(abstract_state.get_id(), move(*selected_split));
}

unique_ptr<Flaw> SplitSelector::pick_split(
    const AbstractState &abstract_state,
    const vector<State> &concrete_states,
    const vector<CartesianSet> &desired_cartesian_sets,
    utils::RandomNumberGenerator &rng) const {
    if (pick != PickSplit::MAX_COVER) {
        vector<Split> splits;
        for (size_t i = 0; i < concrete_states.size(); ++i) {
            get_possible_splits(abstract_state, concrete_states.at(i),
                                desired_cartesian_sets.at(i), splits);
        }
        if (splits.size() == 1) {
            return utils::make_unique_ptr<Flaw>(
                abstract_state.get_id(), move(splits[0]));
        }

        if (pick == PickSplit::RANDOM) {
            return utils::make_unique_ptr<Flaw>(
                abstract_state.get_id(),
                move(*rng.choose(splits)));
        }

        double max_rating = numeric_limits<double>::lowest();
        Split *selected_split = nullptr;
        for (Split &split : splits) {
            double rating = rate_split(abstract_state, split);
            if (rating > max_rating) {
                selected_split = &split;
                max_rating = rating;
            }
        }
        // utils::g_log << "SELECTED: " << *selected_split << endl;
        assert(selected_split);
        return utils::make_unique_ptr<Flaw>(abstract_state.get_id(), move(*selected_split));
    }

    assert(pick == PickSplit::MAX_COVER);
    assert(concrete_states.size() == desired_cartesian_sets.size());

    vector<int> domain_sizes = get_domain_sizes(task_proxy);

    vector<vector<pair<int, set<int>>>> splits(
        task_proxy.get_variables().size());
    for (size_t i = 0; i < concrete_states.size(); ++i) {
        vector<Split> cur_splits;
        get_possible_splits(abstract_state, concrete_states.at(i),
                            desired_cartesian_sets.at(i), cur_splits);

        for (const Split &split : cur_splits) {
            int var = split.var_id;
            int concrete_value =
                concrete_states.at(i)[split.var_id].get_value();
            set<int> values;

            for (int val : split.values) {
                if (abstract_state.contains(var, val)
                    && val != concrete_value)
                    values.insert(val);
            }

            splits[split.var_id].emplace_back(concrete_value, values);
        }
    }

    // Sort by size of desired cartesian sets
    for (size_t var = 0; var < splits.size(); ++var) {
        sort(splits[var].begin(), splits[var].end(),
             [](const pair<int, set<int>> &a,
                const pair<int, set<int>> &b) -> bool
             {
                 return a.second.size() > b.second.size();
             });
    }

    // Compute prio for each split
    // TODO(speckd): change to map lookup
    vector<vector<int>> split_prio(task_proxy.get_variables().size());
    for (size_t var = 0; var < split_prio.size(); ++var) {
        split_prio[var].resize(splits[var].size(), 0);
        for (size_t i = 0; i < splits[var].size(); ++i) {
            for (size_t j = i + 1; j < splits[var].size(); ++j) {
                // int i_value = splits[var][i].first;
                const set<int> &i_set = splits[var][i].second;
                int j_value = splits[var][j].first;
                const set<int> &j_set = splits[var][j].second;

                // is subset of and value not contained
                if (includes(i_set.begin(), i_set.end(),
                             j_set.begin(), j_set.end())
                    && i_set.count(j_value) == 0) {
                    ++split_prio[var][i];
                }
            }
        }
    }

    if (debug) {
        for (size_t var = 0; var < split_prio.size(); ++var) {
            utils::g_log << var << ": [";
            for (size_t i = 0; i < splits[var].size(); ++i) {
                int value = splits[var][i].first;
                vector<int> c_set = vector<int>(splits[var][i].second.begin(),
                                                splits[var][i].second.end());
                int prio = split_prio[var][i];
                cout << "<val=" << value << "," << c_set << ",prio=" << prio << ">, ";
            }
            utils::g_log << "]" << endl;
        }
        cout << endl;
    }

    int best_var_id = -1;
    int best_split_id = -1;
    int best_prio = -1;
    for (size_t var = 0; var < split_prio.size(); ++var) {
        for (size_t split_id = 0; split_id < split_prio[var].size(); ++split_id) {
            if (split_prio[var][split_id] > best_prio) {
                best_var_id = var;
                best_split_id = split_id;
                best_prio = split_prio[var][split_id];
            }
        }
    }

    Split split(best_var_id,
                -1, // TODO: pass correct value.
                vector<int>(splits[best_var_id][best_split_id].second.begin(),
                            splits[best_var_id][best_split_id].second.end()));
    return utils::make_unique_ptr<Flaw>(abstract_state.get_id(), move(split));
}
}

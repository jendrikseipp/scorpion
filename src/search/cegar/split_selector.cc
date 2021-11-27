#include "split_selector.h"

#include "abstract_state.h"
#include "utils.h"

#include "../heuristics/additive_heuristic.h"

#include "../utils/memory.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace std;

namespace cegar {
bool Split::combine_with(Split &&other) {
    assert(var_id == other.var_id);
    if (*this == other) {
        return true;
    }
    // Try to switch the order to enable merging the splits.
    if (values.size() == 1 && values[0] == other.value) {
        swap(value, values[0]);
        assert(value == other.value);
    } else if (other.values.size() == 1 && value == other.values[0]) {
        swap(other.value, other.values[0]);
        assert(value == other.value);
    } else if (values.size() == 1 && other.values.size() == 1 && values[0] == other.values[0]) {
        swap(value, values[0]);
        swap(other.value, other.values[0]);
        assert(value == other.value);
    }

    if (value == other.value) {
        assert(utils::is_sorted_unique(values));
        assert(utils::is_sorted_unique(other.values));
        vector<int> combined_values;
        set_union(values.begin(), values.end(),
                  other.values.begin(), other.values.end(),
                  back_inserter(combined_values));
        swap(values, combined_values);
        return true;
    } else {
        // TODO: Combine splits that have no common singleton value.
        return false;
    }
}


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
            splits.emplace_back(abstract_state.get_id(), var_id, fact.value, move(wanted));
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

unique_ptr<Split> SplitSelector::pick_split(
    const AbstractState &abstract_state,
    const State &concrete_state,
    const CartesianSet &desired_cartesian_set,
    utils::RandomNumberGenerator &rng) const {
    vector<Split> splits;
    get_possible_splits(abstract_state, concrete_state,
                        desired_cartesian_set, splits);
    assert(!splits.empty());

    if (splits.size() == 1) {
        return utils::make_unique_ptr<Split>(move(splits[0]));
    }

    if (pick == PickSplit::RANDOM) {
        return utils::make_unique_ptr<Split>(move(*rng.choose(splits)));
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
    assert(selected_split);
    return utils::make_unique_ptr<Split>(move(*selected_split));
}

unique_ptr<Split> SplitSelector::pick_split(
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
            return utils::make_unique_ptr<Split>(move(splits[0]));
        }

        if (pick == PickSplit::RANDOM) {
            return utils::make_unique_ptr<Split>(move(*rng.choose(splits)));
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
        return utils::make_unique_ptr<Split>(move(*selected_split));
    }

    assert(pick == PickSplit::MAX_COVER);
    assert(concrete_states.size() == desired_cartesian_sets.size());

    vector<int> domain_sizes = get_domain_sizes(task_proxy);

    vector<Split> all_splits;
    for (size_t i = 0; i < concrete_states.size(); ++i) {
        get_possible_splits(abstract_state, concrete_states.at(i),
                            desired_cartesian_sets.at(i), all_splits);
    }

    vector<vector<pair<Split, int>>> unique_splits_by_var(task_proxy.get_variables().size());
    for (const Split &split : all_splits) {
        vector<pair<Split, int>> &split_counts = unique_splits_by_var[split.var_id];
        bool is_duplicate = false;
        for (auto &pair : split_counts) {
            if (pair.first == split) {
                is_duplicate = true;
                ++pair.second;
            }
        }
        if (!is_duplicate) {
            split_counts.emplace_back(move(split), 1);
        }
    }

    if (debug) {
        cout << "Unsorted splits: " << endl;
        for (auto &split_counts : unique_splits_by_var) {
            utils::g_log << " [";
            for (auto &pair : split_counts) {
                const Split &split = pair.first;
                int count = pair.second;
                cout << split << ": " << count << ", ";
            }
            cout << "]" << endl;
        }
    }


    for (auto &split_counts : unique_splits_by_var) {
        if (split_counts.size() <= 1) {
            continue;
        }
        // Sort splits by the number of covered flaws.
        sort(split_counts.begin(), split_counts.end(),
             [](const pair<Split, int> &pair1, const pair<Split, int> &pair2) {
                 return pair1.second > pair2.second;
             });
        // Try to merge each split into first split.
        Split &best_split_for_var = split_counts[0].first;
        for (size_t i = 1; i < split_counts.size(); ++i) {
            if (debug) {
                cout << "Combine " << best_split_for_var << " with " << split_counts[i].first;
            }
            bool combined = best_split_for_var.combine_with(move(split_counts[i].first));
            if (debug) {
                cout << " --> " << combined << endl;
            }
            if (combined) {
                split_counts[0].second += split_counts[i].second;
            }
        }
        split_counts.erase(split_counts.begin() + 1, split_counts.end());
    }

    if (debug) {
        cout << "Sorted and combined splits: " << endl;
        for (auto &split_counts : unique_splits_by_var) {
            utils::g_log << " [";
            for (auto &pair : split_counts) {
                const Split &split = pair.first;
                int count = pair.second;
                cout << split << ": " << count << ", ";
            }
            cout << "]" << endl;
        }
    }

    Split *best_split = nullptr;
    int max_count = -1;
    for (auto &split_counts : unique_splits_by_var) {
        if (!split_counts.empty()) {
            Split &best_split_for_var = split_counts[0].first;
            int count = split_counts[0].second;
            if (count > max_count) {
                max_count = count;
                best_split = &best_split_for_var;
            }
        }
    }
    assert(best_split);
    if (debug) {
        utils::g_log << "Best split: " << *best_split << endl;
        cout << endl;
    }
    return utils::make_unique_ptr<Split>(move(*best_split));
}
}

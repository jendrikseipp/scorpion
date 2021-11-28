#include "split_selector.h"

#include "abstract_state.h"
#include "flaw.h"
#include "utils.h"

#include "../heuristics/additive_heuristic.h"

#include "../utils/logging.h"
#include "../utils/memory.h"
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
    vector<Split> &&splits,
    utils::RandomNumberGenerator &rng) const {
    assert(!splits.empty());
    if (pick != PickSplit::MAX_COVER) {
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

    assert(pick == PickSplit::MAX_COVER);

    vector<int> domain_sizes = get_domain_sizes(task_proxy);

    vector<vector<Split>> unique_splits_by_var(task_proxy.get_variables().size());
    for (Split &new_split : splits) {
        vector<Split> &var_splits = unique_splits_by_var[new_split.var_id];
        bool is_duplicate = false;
        for (auto &old_split : var_splits) {
            if (old_split == new_split) {
                is_duplicate = true;
                ++old_split.count;
            }
        }
        if (!is_duplicate) {
            var_splits.push_back(move(new_split));
        }
    }

    if (debug) {
        cout << "Unsorted splits: " << endl;
        for (auto &var_splits : unique_splits_by_var) {
            utils::g_log << " " << var_splits << endl;
        }
    }


    for (auto &var_splits : unique_splits_by_var) {
        if (var_splits.size() <= 1) {
            continue;
        }
        // Sort splits by the number of covered flaws.
        sort(var_splits.begin(), var_splits.end(),
             [](const Split &split1, const Split &split2) {
                 return split1.count > split2.count;
             });
        // Try to merge each split into first split.
        Split &best_split_for_var = var_splits[0];
        for (size_t i = 1; i < var_splits.size(); ++i) {
            if (debug) {
                cout << "Combine " << best_split_for_var << " with " << var_splits[i];
            }
            bool combined = best_split_for_var.combine_with(move(var_splits[i]));
            if (debug) {
                cout << " --> " << combined << endl;
            }
            if (combined) {
                var_splits[0].count += var_splits[i].count;
            }
        }
        var_splits.erase(var_splits.begin() + 1, var_splits.end());
    }

    if (debug) {
        cout << "Sorted and combined splits: " << endl;
        for (auto &var_splits : unique_splits_by_var) {
            utils::g_log << " " << var_splits << endl;
        }
    }

    Split *best_split = nullptr;
    int max_count = -1;
    for (auto &var_splits : unique_splits_by_var) {
        if (!var_splits.empty()) {
            Split &best_split_for_var = var_splits[0];
            if (best_split_for_var.count > max_count) {
                max_count = best_split_for_var.count;
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

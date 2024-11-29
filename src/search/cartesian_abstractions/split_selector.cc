#include "split_selector.h"

#include "abstract_state.h"
#include "flaw.h"
#include "utils.h"

#include "../heuristics/additive_heuristic.h"

#include "../plugins/plugin.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace std;

namespace cartesian_abstractions {
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
        // For now, we only combine splits that have a common singleton value.
        return false;
    }
}


SplitSelector::SplitSelector(
    const shared_ptr<AbstractTask> &task,
    PickSplit pick,
    PickSplit tiebreak_pick,
    bool debug)
    : task(task),
      task_proxy(*task),
      debug(debug),
      first_pick(pick),
      tiebreak_pick(tiebreak_pick) {
    if (first_pick == PickSplit::MIN_HADD || first_pick == PickSplit::MAX_HADD ||
        tiebreak_pick == PickSplit::MIN_HADD || tiebreak_pick == PickSplit::MAX_HADD) {
        additive_heuristic =
            utils::make_unique_ptr<additive_heuristic::AdditiveHeuristic>(
                tasks::AxiomHandlingType::APPROXIMATE_NEGATIVE, task,
                false, "h^add within CEGAR abstractions",
                utils::Verbosity::SILENT);
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

double SplitSelector::rate_split(
    const AbstractState &state, const Split &split, PickSplit pick) const {
    int var_id = split.var_id;
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
        rating = -get_min_hadd_value(var_id, split.values);
        break;
    case PickSplit::MAX_HADD:
        rating = get_max_hadd_value(var_id, split.values);
        break;
    case PickSplit::MIN_CG:
        rating = -var_id;
        break;
    case PickSplit::MAX_CG:
        rating = var_id;
        break;
    default:
        cerr << "Invalid pick strategy for rate_split(): "
             << static_cast<int>(pick) << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
    return rating;
}

vector<Split> SplitSelector::compute_max_cover_splits(
    vector<vector<Split>> &&splits) const {
    vector<int> domain_sizes = get_domain_sizes(task_proxy);

    if (debug) {
        cout << "Unsorted splits: " << endl;
        for (auto &var_splits : splits) {
            if (!var_splits.empty()) {
                utils::g_log << " " << var_splits << endl;
            }
        }
    }

    for (auto &var_splits : splits) {
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
        for (auto &var_splits : splits) {
            if (!var_splits.empty()) {
                utils::g_log << " " << var_splits << endl;
            }
        }
    }

    vector<Split> best_splits;
    int max_count = -1;
    for (auto &var_splits : splits) {
        if (!var_splits.empty()) {
            Split &best_split_for_var = var_splits[0];
            if (best_split_for_var.count > max_count) {
                best_splits.clear();
                best_splits.push_back(move(best_split_for_var));
                max_count = best_split_for_var.count;
            } else if (best_split_for_var.count == max_count) {
                best_splits.push_back(move(best_split_for_var));
            }
        }
    }
    return best_splits;
}

vector<Split> SplitSelector::reduce_to_best_splits(
    const AbstractState &abstract_state,
    vector<vector<Split>> &&splits) const {
    if (first_pick == PickSplit::MAX_COVER) {
        return compute_max_cover_splits(move(splits));
    }

    vector<Split> best_splits;
    double max_rating = numeric_limits<double>::lowest();
    for (auto &var_splits : splits) {
        if (!var_splits.empty()) {
            for (Split &split : var_splits) {
                double rating = rate_split(abstract_state, split, first_pick);
                if (rating > max_rating) {
                    best_splits.clear();
                    best_splits.push_back(move(split));
                    max_rating = rating;
                } else if (rating == max_rating) {
                    best_splits.push_back(move(split));
                }
            }
        }
    }
    assert(!best_splits.empty());
    return best_splits;
}

Split SplitSelector::select_from_best_splits(
    const AbstractState &abstract_state,
    vector<Split> &&splits,
    utils::RandomNumberGenerator &rng) const {
    assert(!splits.empty());
    if (splits.size() == 1) {
        return move(splits[0]);
    } else if (tiebreak_pick == PickSplit::RANDOM) {
        return move(*rng.choose(splits));
    }
    double max_rating = numeric_limits<double>::lowest();
    Split *selected_split = nullptr;
    for (Split &split : splits) {
        double rating = rate_split(abstract_state, split, tiebreak_pick);
        if (rating > max_rating) {
            selected_split = &split;
            max_rating = rating;
        }
    }
    assert(selected_split);
    return move(*selected_split);
}

Split SplitSelector::pick_split(
    const AbstractState &abstract_state,
    vector<vector<Split>> &&splits,
    utils::RandomNumberGenerator &rng) const {
    if (first_pick == PickSplit::RANDOM) {
        vector<int> vars_with_splits;
        for (size_t var = 0; var < splits.size(); ++var) {
            auto &var_splits = splits[var];
            if (!var_splits.empty()) {
                vars_with_splits.push_back(var);
            }
        }
        int random_var = *rng.choose(vars_with_splits);
        return move(*rng.choose(splits[random_var]));
    }

    vector<Split> best_splits = reduce_to_best_splits(abstract_state, move(splits));
    assert(!best_splits.empty());
    if (debug) {
        utils::g_log << "Best splits: " << best_splits << endl;
    }
    Split selected_split = select_from_best_splits(abstract_state, move(best_splits), rng);
    if (debug) {
        utils::g_log << "Selected split: " << selected_split << endl;
    }
    return selected_split;
}

static plugins::TypedEnumPlugin<PickSplit> _enum_plugin({
        {"random",
         "select a random variable (among all eligible variables)"},
        {"min_unwanted",
         "select an eligible variable which has the least unwanted values "
         "(number of values of v that land in the abstract state whose "
         "h-value will probably be raised) in the flaw state"},
        {"max_unwanted",
         "select an eligible variable which has the most unwanted values "
         "(number of values of v that land in the abstract state whose "
         "h-value will probably be raised) in the flaw state"},
        {"min_refined",
         "select an eligible variable which is the least refined "
         "(-1 * (remaining_values(v) / original_domain_size(v))) "
         "in the flaw state"},
        {"max_refined",
         "select an eligible variable which is the most refined "
         "(-1 * (remaining_values(v) / original_domain_size(v))) "
         "in the flaw state"},
        {"min_hadd",
         "select an eligible variable with minimal h^add(s_0) value "
         "over all facts that need to be removed from the flaw state"},
        {"max_hadd",
         "select an eligible variable with maximal h^add(s_0) value "
         "over all facts that need to be removed from the flaw state"},
        {"min_cg",
         "order by increasing position in partial ordering of causal graph"},
        {"max_cg",
         "order by decreasing position in partial ordering of causal graph"},
        {"max_cover",
         "compute split that covers the maximum number of flaws for several concrete states."}
    });
}

#ifndef CEGAR_SPLIT_SELECTOR_H
#define CEGAR_SPLIT_SELECTOR_H

#include "abstraction.h"
#include "cartesian_set.h"

#include "../task_proxy.h"

#include <memory>
#include <vector>

namespace additive_heuristic {
class AdditiveHeuristic;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cegar {
class AbstractState;
class Flaw;

// Strategies for selecting a split in case there are multiple possibilities.
enum class PickSplit {
    RANDOM,
    // Number of values that land in the state whose h-value is probably raised.
    MIN_UNWANTED,
    MAX_UNWANTED,
    // Refinement: - (remaining_values / original_domain_size)
    MIN_REFINED,
    MAX_REFINED,
    // Compare the h^add(s_0) values of the facts.
    MIN_HADD,
    MAX_HADD,
    // Max Cover rating multiple different concrete states
    MAX_COVER
};


struct Split {
    const int var_id;
    const std::vector<int> values;

    Split(int var_id, std::vector<int> &&values)
        : var_id(var_id), values(move(values)) {
    }

    bool operator==(const Split &other) const {
        return var_id == other.var_id && values == other.values;
    }

    friend std::ostream &operator<<(std::ostream &os, const Split &s) {
        std::string split_values = "{";
        for (size_t i = 0; i < s.values.size(); ++i) {
            if (i != 0)
                split_values += ", ";
            split_values += std::to_string(s.values[i]);
        }
        split_values += "}";
        return os << "<" << s.var_id << "," << split_values << ">";
    }
};


/*
  Select split in case there are multiple possible splits.
*/
class SplitSelector {
    const std::shared_ptr<AbstractTask> task;
    const TaskProxy task_proxy;
    std::unique_ptr<additive_heuristic::AdditiveHeuristic> additive_heuristic;

    const PickSplit pick;

    int get_num_unwanted_values(const AbstractState &state, const Split &split) const;
    double get_refinedness(const AbstractState &state, int var_id) const;
    int get_hadd_value(int var_id, int value) const;
    int get_min_hadd_value(int var_id, const std::vector<int> &values) const;
    int get_max_hadd_value(int var_id, const std::vector<int> &values) const;

    void get_possible_splits(const AbstractState &abstract_state,
                             const State &concrete_state,
                             const CartesianSet &desired_cartesian_set,
                             std::vector<Split> &splits) const;

    double rate_split(const AbstractState &state, const Split &split) const;

public:
    SplitSelector(const std::shared_ptr<AbstractTask> &task,
                  PickSplit pick);
    ~SplitSelector();

    PickSplit get_pick_split_strategy() const {
        return pick;
    }

    std::unique_ptr<Flaw> pick_split(const AbstractState &abstract_state,
                                     const State &concrete_state,
                                     const CartesianSet &desired_cartesian_set,
                                     utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw> pick_split(
        const AbstractState &abstract_state,
        const std::vector<State> &concrete_states,
        const std::vector<CartesianSet> &desired_cartesian_sets) const;
};
}

#endif

#ifndef CEGAR_SPLIT_SELECTOR_H
#define CEGAR_SPLIT_SELECTOR_H

#include "abstraction.h"
#include "cartesian_set.h"

#include "../task_proxy.h"

#include "../utils/logging.h"

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
    // Members are logically const but declaring them as such prevents moving them.
    int var_id;
    int value;
    std::vector<int> values;

    Split(int var_id, int value, std::vector<int> &&values)
        : var_id(var_id), value(value), values(move(values)) {
    }

    bool combine_with(Split &&other);

    bool operator==(const Split &other) const {
        assert(var_id == other.var_id);
        if (value == other.value) {
            return values == other.values;
        } else if (values.size() == 1 && other.values.size() == 1) {
            // If we need to separate exactly two values, their order doesn't matter.
            return value == other.values[0] && other.value == values[0];
        } else {
            return false;
        }
    }

    friend std::ostream &operator<<(std::ostream &os, const Split &s) {
        return os << "<" << s.var_id << "=" << s.value << "|" << s.values << ">";
    }
};


/*
  Select split in case there are multiple possible splits.
*/
class SplitSelector {
    const std::shared_ptr<AbstractTask> task;
    const TaskProxy task_proxy;
    const bool debug;
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
    SplitSelector(
        const std::shared_ptr<AbstractTask> &task,
        PickSplit pick,
        bool debug);
    ~SplitSelector();

    PickSplit get_pick_split_strategy() const {
        return pick;
    }

    std::unique_ptr<Flaw> pick_split(
        const AbstractState &abstract_state,
        const State &concrete_state,
        const CartesianSet &desired_cartesian_set,
        utils::RandomNumberGenerator &rng) const;

    std::unique_ptr<Flaw> pick_split(
        const AbstractState &abstract_state,
        const std::vector<State> &concrete_states,
        const std::vector<CartesianSet> &desired_cartesian_sets,
        utils::RandomNumberGenerator &rng) const;
};
}

#endif

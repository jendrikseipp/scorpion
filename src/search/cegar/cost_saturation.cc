#include "cost_saturation.h"

#include "abstraction.h"
#include "additive_cartesian_heuristic.h"
#include "subtask_generators.h"
#include "transition_system.h"
#include "utils.h"

#include "../globals.h"

#include "../task_utils/task_properties.h"
#include "../tasks/modified_operator_costs_task.h"
#include "../utils/countdown_timer.h"
#include "../utils/logging.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace cegar {
// TODO: Remove.
int hacked_num_landmark_abstractions = -1;

void reduce_costs(
    vector<int> &remaining_costs, const vector<int> &saturated_costs) {
    assert(remaining_costs.size() == saturated_costs.size());
    for (size_t i = 0; i < remaining_costs.size(); ++i) {
        int &remaining = remaining_costs[i];
        const int &saturated = saturated_costs[i];
        assert(saturated <= remaining);
        /* Since we ignore transitions from states s with h(s)=INF, all
           saturated costs (h(s)-h(s')) are finite or -INF. */
        assert(saturated != INF);
        if (remaining == INF) {
            // INF - x = INF for finite values x.
        } else if (saturated == -INF) {
            remaining = INF;
        } else {
            remaining -= saturated;
        }
        assert(remaining >= 0);
    }
}

CostSaturation::CostSaturation(
    CostPartitioningType cost_partitioning_type,
    const vector<shared_ptr<SubtaskGenerator>> &subtask_generators,
    int max_states,
    int max_non_looping_transitions,
    double max_time,
    bool use_general_costs,
    bool exclude_abstractions_with_zero_init_h,
    PickSplit pick_split,
    utils::RandomNumberGenerator &rng)
    : cost_partitioning_type(cost_partitioning_type),
      subtask_generators(subtask_generators),
      max_states(max_states),
      max_non_looping_transitions(max_non_looping_transitions),
      max_time(max_time),
      use_general_costs(use_general_costs),
      exclude_abstractions_with_zero_init_h(exclude_abstractions_with_zero_init_h),
      pick_split(pick_split),
      rng(rng),
      num_abstractions(0),
      num_states(0),
      num_non_looping_transitions(0) {
}

void CostSaturation::initialize(const shared_ptr<AbstractTask> &task) {
    utils::CountdownTimer timer(max_time);

    TaskProxy task_proxy(*task);

    task_properties::verify_no_axioms(task_proxy);
    task_properties::verify_no_conditional_effects(task_proxy);

    reset(task_proxy);

    function<bool()> should_abort =
        [&] () {
            return num_states >= max_states ||
                   num_non_looping_transitions >= max_non_looping_transitions ||
                   timer.is_expired() ||
                   utils::is_out_of_memory() ||
                   initial_state_is_dead_end();
        };

    for (shared_ptr<SubtaskGenerator> subtask_generator : subtask_generators) {
        SharedTasks subtasks = subtask_generator->get_subtasks(task);
        build_abstractions(subtasks, timer, should_abort);
        if (hacked_num_landmark_abstractions == -1) {
            hacked_num_landmark_abstractions = abstractions.size();
        }
        if (should_abort())
            break;
    }
    print_statistics();
}

vector<unique_ptr<Abstraction>> CostSaturation::extract_abstractions() {
    return move(abstractions);
}

vector<shared_ptr<TransitionSystem>> CostSaturation::extract_transition_systems() {
    return move(transition_systems);
}

void CostSaturation::reset(const TaskProxy &task_proxy) {
    remaining_costs = task_properties::get_operator_costs(task_proxy);
    num_abstractions = 0;
    num_states = 0;
}

shared_ptr<AbstractTask> CostSaturation::get_remaining_costs_task(
    shared_ptr<AbstractTask> &parent) const {
    vector<int> costs = remaining_costs;
    return make_shared<extra_tasks::ModifiedOperatorCostsTask>(
        parent, move(costs));
}

bool CostSaturation::initial_state_is_dead_end() const {
    return any_of(abstractions.begin(), abstractions.end(),
                  [](const unique_ptr<Abstraction> &abstraction) {
            return abstraction->get_h_value_of_initial_state() == INF;
        });
}

void CostSaturation::build_abstractions(
    const vector<shared_ptr<AbstractTask>> &subtasks,
    const utils::CountdownTimer &timer,
    function<bool()> should_abort) {
    int rem_subtasks = subtasks.size();
    for (shared_ptr<AbstractTask> subtask : subtasks) {
        subtask = get_remaining_costs_task(subtask);

        assert(num_states < max_states);
        unique_ptr<Abstraction> abstraction = utils::make_unique_ptr<Abstraction>(
            subtask,
            max(1, (max_states - num_states) / rem_subtasks),
            max(1, (max_non_looping_transitions - num_non_looping_transitions) /
                rem_subtasks),
            timer.get_remaining_time() / rem_subtasks,
            use_general_costs,
            pick_split,
            rng);

        ++num_abstractions;
        num_states += abstraction->get_num_states();
        assert(num_states <= max_states);
        num_non_looping_transitions += abstraction->get_num_non_looping_transitions();

        if (cost_partitioning_type == CostPartitioningType::SATURATED) {
            reduce_costs(remaining_costs, abstraction->get_saturated_costs());
        }

        if (cost_partitioning_type == CostPartitioningType::SATURATED ||
            cost_partitioning_type == CostPartitioningType::SATURATED_POSTHOC ||
            cost_partitioning_type == CostPartitioningType::SATURATED_MAX) {
            int init_h = abstraction->get_h_value_of_initial_state();
            if (!exclude_abstractions_with_zero_init_h || init_h > 0) {
                abstractions.push_back(move(abstraction));
            }
        } else if (cost_partitioning_type == CostPartitioningType::OPTIMAL) {
            transition_systems.push_back(
                make_shared<TransitionSystem>(move(*abstraction)));
        } else {
            ABORT("Invalid cost partitioning type");
        }
        if (should_abort())
            break;

        --rem_subtasks;
    }
}

void CostSaturation::print_statistics() const {
    g_log << "Done initializing additive Cartesian heuristic" << endl;
    cout << "Cartesian abstractions built: " << num_abstractions << endl;
    cout << "Abstractions stored: " << abstractions.size() << endl;
    cout << "Transition systems stored: " << transition_systems.size() << endl;
    cout << "Cartesian states: " << num_states << endl;
    cout << "Total number of non-looping transitions: "
         << num_non_looping_transitions << endl;
    cout << endl;
}
}

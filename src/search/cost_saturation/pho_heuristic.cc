#include "pho_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "max_cost_partitioning_heuristic.h"
#include "uniform_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/execution.h"
#include "../utils/logging.h"

using namespace std;

namespace cost_saturation {
/*
  The implementation currently computes weighted lookup tables for PhO and
  holds them in memory. A more efficient implementation would only store the
  weights and compute the weighted heuristic values on the fly when evaluating
  a state.
*/
PhO::PhO(
    const Abstractions &abstractions, const vector<int> &costs,
    lp::LPSolverType solver_type, bool saturated, const utils::LogProxy &log)
    : lp_solver(solver_type),
      abstraction_has_unsolvable_states(abstractions.size(), false),
      log(log) {
    double infinity = lp_solver.get_infinity();
    int num_abstractions = abstractions.size();
    int num_operators = costs.size();

    vector<vector<int>> saturated_costs_by_abstraction;
    saturated_costs_by_abstraction.reserve(num_abstractions);
    h_values_by_abstraction.reserve(num_abstractions);
    for (int i = 0; i < num_abstractions; ++i) {
        const Abstraction &abstraction = *abstractions[i];
        vector<int> h_values = abstraction.compute_goal_distances(costs);
        abstraction_has_unsolvable_states[i] =
            any_of(utils::unseq, h_values.begin(), h_values.end(), [](int x) {
                return x == cost_saturation::INF;
            });
        vector<int> saturated_costs =
            abstraction.compute_saturated_costs(h_values);
        h_values_by_abstraction.push_back(move(h_values));
        saturated_costs_by_abstraction.push_back(move(saturated_costs));
    }

    named_vector::NamedVector<lp::LPVariable> variables;
    variables.reserve(num_abstractions);
    for (int i = 0; i < num_abstractions; ++i) {
        // Objective coefficients are set below.
        variables.emplace_back(0, infinity, 0);
    }

    named_vector::NamedVector<lp::LPConstraint> constraints;
    constraints.reserve(num_operators);
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        lp::LPConstraint constraint(
            lp::LPConstraintSense::LESS_EQUAL, costs[op_id]);
        for (int i = 0; i < num_abstractions; ++i) {
            if (saturated) {
                int scf_h = saturated_costs_by_abstraction[i][op_id];
                if (scf_h == -INF) {
                    // The constraint is always satisfied and we can ignore it.
                    continue;
                }
                if (scf_h != 0) {
                    constraint.insert(i, scf_h);
                }
            } else if (
                abstractions[i]->operator_is_active(op_id) &&
                costs[op_id] != 0) {
                constraint.insert(i, costs[op_id]);
            }
        }
        if (!constraint.empty()) {
            constraints.push_back(move(constraint));
        }
    }

    lp::LinearProgram lp(
        lp::LPObjectiveSense::MAXIMIZE, move(variables), move(constraints),
        lp_solver.get_infinity());
    lp_solver.load_problem(lp);
}

CostPartitioningHeuristic PhO::compute_cost_partitioning(
    const Abstractions &abstractions, const vector<int> &,
    const vector<int> &costs, const vector<int> &abstract_state_ids) {
    int num_abstractions = abstractions.size();
    int num_operators = costs.size();

    for (int i = 0; i < num_abstractions; ++i) {
        int h = h_values_by_abstraction[i][abstract_state_ids[i]];
        if (h == INF) {
            // State is unsolvable.
            vector<int> zero_costs(num_operators, 0);
            CostPartitioningHeuristic cp_heuristic;
            for (int i = 0; i < num_abstractions; ++i) {
                vector<int> h_values =
                    abstractions[i]->compute_goal_distances(zero_costs);
                cp_heuristic.add_h_values(i, move(h_values));
            }
            return cp_heuristic;
        }
        lp_solver.set_objective_coefficient(i, h);
    }

    lp_solver.solve();
    assert(lp_solver.has_optimal_solution());
    vector<double> solution = lp_solver.extract_solution();
    if (log.is_at_least_debug()) {
        log << "Objective value: " << lp_solver.get_objective_value() << endl;
        log << "Solution: " << solution << endl;
    }

    CostPartitioningHeuristic cp_heuristic;
    for (int i = 0; i < num_abstractions; ++i) {
        double weight = solution[i];
        if (weight == 0.0 && !abstraction_has_unsolvable_states[i]) {
            // This abstraction is assigned a weight of zero and has no
            // unsolvable states, so we can skip it.
            continue;
        }
        vector<int> weighted_h_values;
        weighted_h_values.reserve(h_values_by_abstraction[i].size());
        for (int h : h_values_by_abstraction[i]) {
            assert(weight > 0.0);
            weighted_h_values.push_back(
                h == INF ? INF : static_cast<int>(weight * h));
        }
        cp_heuristic.add_h_values(i, move(weighted_h_values));
    }
    if (log.is_at_least_debug()) {
        log << "CP value: "
            << cp_heuristic.compute_heuristic(abstract_state_ids) << endl;
    }
    return cp_heuristic;
}

PhoHeuristic::PhoHeuristic(
    const shared_ptr<AbstractTask> &task,
    const vector<shared_ptr<AbstractionGenerator>> &abstraction_generators,
    bool saturated, lp::LPSolverType lpsolver,
    const shared_ptr<OrderGenerator> &order_generator, int max_orders,
    int max_size_kb, double max_time, bool diversify, int num_samples,
    double max_optimization_time, int random_seed, bool cache_estimates,
    const string &description, utils::Verbosity verbosity)
    : ScaledCostPartitioningHeuristic(
          task, cache_estimates, description, verbosity) {
    /* Our base class replaced the task by a copy with scaled costs, so we use
       the scaled task (this->task) for all computations below. */
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    Abstractions abstractions =
        generate_abstractions(this->task, abstraction_generators);
    PhO pho(abstractions, costs, lpsolver, saturated, log);
    CPFunction cp_function =
        [&pho](
            const Abstractions &abstractions_, const vector<int> &order_,
            const vector<int> &costs_, const vector<int> &abstract_state_ids) {
            return pho.compute_cost_partitioning(
                abstractions_, order_, costs_, abstract_state_ids);
        };
    CPHeuristics cp_heuristics = compute_cp_heuristics(
        task_proxy, abstractions, costs, cp_function, order_generator,
        max_orders, max_size_kb, max_time, diversify, num_samples,
        max_optimization_time, random_seed);
    // TODO: extract dead ends.
    set_cost_partitionings(move(abstractions), move(cp_heuristics), nullptr);
}

class PhoFeature : public plugins::TypedFeature<TaskIndependentEvaluator> {
public:
    PhoFeature() : TypedFeature("pho") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Post-hoc optimization heuristic");
        document_synopsis(
            "Compute the maximum over multiple PhO heuristics precomputed offline.");

        add_options_for_cost_partitioning_heuristic(*this, "pho");
        add_option<bool>("saturated", "saturate costs", "true");
        add_order_options(*this);
        lp::add_lp_solver_option_to_feature(*this);
    }

    virtual shared_ptr<TaskIndependentEvaluator> create_component(
        const plugins::Options &options) const override {
        return components::make_auto_task_independent_component<
            PhoHeuristic, Evaluator>(
            get_abstraction_generator_list_from_options(options),
            options.get<bool>("saturated"),
            lp::get_lp_solver_arguments_from_options(options),
            get_order_arguments_from_options(options),
            get_heuristic_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<PhoFeature> _plugin;
}

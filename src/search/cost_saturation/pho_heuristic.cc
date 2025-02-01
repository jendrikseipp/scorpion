#include "pho_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic_collection_generator.h"
#include "cost_partitioning_heuristic.h"
#include "max_cost_partitioning_heuristic.h"
#include "uniform_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../algorithms/partial_state_tree.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
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
    const Abstractions &abstractions,
    const vector<int> &costs,
    lp::LPSolverType solver_type,
    bool saturated,
    const utils::LogProxy &log)
    : lp_solver(solver_type),
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
        vector<int> saturated_costs = abstraction.compute_saturated_costs(h_values);
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
        lp::LPConstraint constraint(-infinity, costs[op_id]);
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
            } else if (abstractions[i]->operator_is_active(op_id) && costs[op_id] != 0) {
                constraint.insert(i, costs[op_id]);
            }
        }
        if (!constraint.empty()) {
            constraints.push_back(move(constraint));
        }
    }

    lp::LinearProgram lp(lp::LPObjectiveSense::MAXIMIZE, move(variables), move(constraints),
                         lp_solver.get_infinity());
    lp_solver.load_problem(lp);
}

CostPartitioningHeuristic PhO::compute_cost_partitioning(
    const Abstractions &abstractions,
    const vector<int> &,
    const vector<int> &costs,
    const vector<int> &abstract_state_ids) {
    int num_abstractions = abstractions.size();
    int num_operators = costs.size();

    for (int i = 0; i < num_abstractions; ++i) {
        int h = h_values_by_abstraction[i][abstract_state_ids[i]];
        lp_solver.set_objective_coefficient(i, h);
    }
    lp_solver.solve();

    if (!lp_solver.has_optimal_solution()) {
        // State is unsolvable.
        vector<int> zero_costs(num_operators, 0);
        CostPartitioningHeuristic cp_heuristic;
        for (int i = 0; i < num_abstractions; ++i) {
            vector<int> h_values = abstractions[i]->compute_goal_distances(zero_costs);
            cp_heuristic.add_h_values(i, move(h_values));
        }
        return cp_heuristic;
    }

    vector<double> solution = lp_solver.extract_solution();
    if (log.is_at_least_debug()) {
        log << "Objective value: " << lp_solver.get_objective_value() << endl;
        log << "Solution: " << solution << endl;
    }
    CostPartitioningHeuristic cp_heuristic;
    for (int i = 0; i < num_abstractions; ++i) {
        double weight = solution[i];
        if (weight == 0.0) {
            // This abstraction is assigned a weight of zero, so we can skip it.
            continue;
        }
        vector<int> weighted_h_values;
        weighted_h_values.reserve(h_values_by_abstraction[i].size());
        for (int h : h_values_by_abstraction[i]) {
            assert(weight > 0.0);
            weighted_h_values.push_back(h == INF ? INF : static_cast<int>(weight * h));
        }
        cp_heuristic.add_h_values(i, move(weighted_h_values));
    }
    if (log.is_at_least_debug()) {
        log << "CP value: " << cp_heuristic.compute_heuristic(abstract_state_ids) << endl;
    }
    return cp_heuristic;
}

class PhoFeature
    : public plugins::TypedFeature<Evaluator, ScaledCostPartitioningHeuristic> {
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

    virtual shared_ptr<ScaledCostPartitioningHeuristic> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        shared_ptr<AbstractTask> scaled_costs_task =
            get_scaled_costs_task(options.get<shared_ptr<AbstractTask>>("transform"));

        TaskProxy task_proxy(*scaled_costs_task);
        vector<int> costs = task_properties::get_operator_costs(task_proxy);
        Abstractions abstractions = generate_abstractions(
            scaled_costs_task, options.get_list<shared_ptr<AbstractionGenerator>>("abstractions"));
        PhO pho(abstractions, costs, options.get<lp::LPSolverType>("lpsolver"),
                options.get<bool>("saturated"),
                utils::get_log_for_verbosity(options.get<utils::Verbosity>("verbosity")));
        CPFunction cp_function = [&pho](const Abstractions &abstractions_,
                                        const vector<int> &order_,
                                        const vector<int> &costs_,
                                        const vector<int> &abstract_state_ids) {
                return pho.compute_cost_partitioning(abstractions_, order_, costs_, abstract_state_ids);
            };
        vector<CostPartitioningHeuristic> cp_heuristics =
            get_cp_heuristic_collection_generator_from_options(options)->generate_cost_partitionings(
                task_proxy, abstractions, costs, cp_function);
        return plugins::make_shared_from_arg_tuples<ScaledCostPartitioningHeuristic>(
            move(abstractions),
            move(cp_heuristics),
            // TODO: extract dead ends.
            nullptr,
            scaled_costs_task, options.get<bool>("cache_estimates"),
            options.get<string>("description"), options.get<utils::Verbosity>("verbosity"));
    }
};

static plugins::FeaturePlugin<PhoFeature> _plugin;
}

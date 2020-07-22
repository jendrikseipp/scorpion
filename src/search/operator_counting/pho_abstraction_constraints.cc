#include "pho_abstraction_constraints.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../cost_saturation/abstraction.h"
#include "../cost_saturation/abstraction_generator.h"
#include "../cost_saturation/utils.h"
#include "../lp/lp_solver.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"

#include <cassert>
#include <limits>

using namespace std;

namespace operator_counting {
PhOAbstractionConstraints::PhOAbstractionConstraints(const Options &opts)
    : abstraction_generators(
          opts.get_list<shared_ptr<cost_saturation::AbstractionGenerator>>(
              "abstractions")),
      saturated(opts.get<bool>("saturated")),
      counting(opts.get<bool>("counting")),
      consider_finite_negative_saturated_costs(
          opts.get<bool>("consider_finite_negative_saturated_costs")),
      forbid_useless_operators(opts.get<bool>("forbid_useless_operators")),
      ignore_goal_out_operators(opts.get<bool>("ignore_goal_out_operators")) {
}

void PhOAbstractionConstraints::initialize_constraints(
    const shared_ptr<AbstractTask> &task,
    vector<lp::LPVariable> &variables,
    vector<lp::LPConstraint> &constraints,
    double infinity) {
    cost_saturation::Abstractions abstractions =
        cost_saturation::generate_abstractions(task, abstraction_generators);
    abstraction_functions.reserve(abstractions.size());
    h_values_by_abstraction.reserve(abstractions.size());

    vector<int> operator_costs = task_properties::get_operator_costs(TaskProxy(*task));
    int num_ops = operator_costs.size();
    constraint_offset = constraints.size();

    vector<vector<int>> local_vars(abstractions.size(), vector<int>(num_ops, -1));
    if (counting) {
        int abstraction_id = 0;
        for (auto &abstraction : abstractions) {
            vector<int> transition_counts =
                abstraction->get_transition_counts(
                    consider_finite_negative_saturated_costs || ignore_goal_out_operators);
            for (int op = 0; op < num_ops; ++op) {
                if (transition_counts[op] > 0) {
                    // Add Y_{h,o} variable with 0 <= Y_{h,o} <= c_{h,o}.
                    local_vars[abstraction_id][op] = variables.size();
                    lp::LPVariable local_count(0.0, transition_counts[op], 0.0);
                    variables.push_back(move(local_count));
                }
            }
            ++abstraction_id;
        }
    }

    // TODO: Remove code duplication.
    if (saturated) {
        vector<bool> useless_operators(operator_costs.size(), false);
        int abstraction_id = 0;
        for (auto &abstraction : abstractions) {
            // Add constraint \sum_{o} Y_o * scf_h(o) >= 0.
            constraints.emplace_back(0, infinity);
            lp::LPConstraint &constraint = constraints.back();
            vector<int> h_values = abstraction->compute_goal_distances(
                operator_costs);
            vector<int> saturated_costs = abstraction->compute_saturated_costs(
                h_values);
            for (size_t op_id = 0; op_id < saturated_costs.size(); ++op_id) {
                if (saturated_costs[op_id] != 0) {
                    if (saturated_costs[op_id] == -cost_saturation::INF) {
                        useless_operators[op_id] = true;
                    } else if (consider_finite_negative_saturated_costs ||
                               saturated_costs[op_id] > 0) {
                        int counting_var = op_id; // Y_o
                        if (counting) {
                            // Get ID of Y_{h,o}.
                            counting_var = local_vars[abstraction_id][op_id];
                            assert(counting_var >= 0);
                        }
                        constraint.insert(counting_var, saturated_costs[op_id]);
                    }
                }
            }
            h_values_by_abstraction.push_back(move(h_values));
            ++abstraction_id;
        }
        if (forbid_useless_operators) {
            // Force operator count of operators o with scf(o)=-\infty to be 0.
            for (size_t op_id = 0; op_id < useless_operators.size(); ++op_id) {
                if (useless_operators[op_id]) {
                    variables[op_id].lower_bound = 0.0;
                    variables[op_id].upper_bound = 0.0;
                }
            }
        }
    } else {
        int abstraction_id = 0;
        for (auto &abstraction : abstractions) {
            constraints.emplace_back(0, infinity);
            lp::LPConstraint &constraint = constraints.back();
            for (size_t op_id = 0; op_id < operator_costs.size(); ++op_id) {
                if (abstraction->operator_is_active(op_id)) {
                    int counting_var = op_id; // Y_o
                    if (counting) {
                        // Get ID of Y_{h,o}.
                        counting_var = local_vars[abstraction_id][op_id];
                    }
                    assert(counting_var >= 0);
                    constraint.insert(counting_var, operator_costs[op_id]);
                }
            }
            h_values_by_abstraction.push_back(
                abstraction->compute_goal_distances(operator_costs));
            ++abstraction_id;
        }
    }

    if (counting) {
        for (size_t abstraction_id = 0; abstraction_id < abstractions.size(); ++abstraction_id) {
            for (int op = 0; op < num_ops; ++op) {
                int local_count_var_id = local_vars[abstraction_id][op];
                if (local_count_var_id >= 0) {
                    // Add constraint Y_{h,o} <= Y_{o} <=> Y_{o} - Y_{h,o} >= 0.
                    lp::LPConstraint constraint(0.0, infinity);
                    constraint.insert(op, 1.0);
                    constraint.insert(local_count_var_id, -1.0);
                    constraints.push_back(move(constraint));
                }
            }
        }
    }

    for (auto &abstraction : abstractions) {
        abstraction_functions.push_back(abstraction->extract_abstraction_function());
    }
}

bool PhOAbstractionConstraints::update_constraints(
    const State &state, lp::LPSolver &lp_solver) {
    for (size_t i = 0; i < abstraction_functions.size(); ++i) {
        int constraint_id = constraint_offset + i;
        int state_id = abstraction_functions[i]->get_abstract_state_id(state);
        assert(utils::in_bounds(i, h_values_by_abstraction));
        const vector<int> &h_values = h_values_by_abstraction[i];
        assert(utils::in_bounds(state_id, h_values));
        int h = h_values[state_id];
        if (h == cost_saturation::INF) {
            return true;
        }
        lp_solver.set_constraint_lower_bound(constraint_id, h);
    }
    return false;
}

static shared_ptr<ConstraintGenerator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Posthoc optimization constraints for abstractions",
        "For each abstraction heuristic h the generator will add the"
        " constraint h(s) <= sum_{o in relevant(h)} Count_o.");

    parser.add_list_option<shared_ptr<cost_saturation::AbstractionGenerator>>(
        "abstractions",
        "abstraction generation methods",
        "[cartesian()]");

    parser.add_option<bool>(
        "saturated",
        "use saturated instead of full operator costs in constraints",
        "false");
    parser.add_option<bool>(
        "counting",
        "add helper variables that limit the number of operator counts per abstraction",
        "false");
    parser.add_option<bool>(
        "consider_finite_negative_saturated_costs",
        "if false, ignore operators with finite negative saturated costs in constraints",
        "true");
    parser.add_option<bool>(
        "forbid_useless_operators",
        "force operator count of operators o with scf(o)=-\\infty to be zero",
        "true");

    parser.add_option<bool>(
        "ignore_goal_out_operators",
        "ignore operators that start in a goal state if counting is true and negative cost is not considered",
        "true");

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<PhOAbstractionConstraints>(opts);
}

static Plugin<ConstraintGenerator> _plugin("pho_abstraction_constraints", _parse);
}

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
              "abstraction_generators")),
      saturated(opts.get<bool>("saturated")) {
}

void PhOAbstractionConstraints::initialize_constraints(
    const shared_ptr<AbstractTask> &task,
    vector<lp::LPConstraint> &constraints,
    double infinity) {
    cost_saturation::Abstractions abstractions =
        cost_saturation::generate_abstractions(task, abstraction_generators);

    vector<int> operator_costs = task_properties::get_operator_costs(TaskProxy(*task));
    constraint_offset = constraints.size();
    // TODO: Remove code duplication.
    if (saturated) {
        for (auto &abstraction : abstractions) {
            constraints.emplace_back(0, infinity);
            lp::LPConstraint &constraint = constraints.back();
            vector<int> h_values = abstraction->compute_goal_distances(
                operator_costs);
            vector<int> saturated_costs = abstraction->compute_saturated_costs(
                h_values);
            for (size_t op_id = 0; op_id < saturated_costs.size(); ++op_id) {
                if (saturated_costs[op_id] > 0) {
                    constraint.insert(op_id, saturated_costs[op_id]);
                }
            }
            h_values_by_abstraction.push_back(move(h_values));
        }
    } else {
        for (auto &abstraction : abstractions) {
            constraints.emplace_back(0, infinity);
            lp::LPConstraint &constraint = constraints.back();
            for (size_t op_id = 0; op_id < operator_costs.size(); ++op_id) {
                if (abstraction->operator_is_active(op_id)) {
                    assert(utils::in_bounds(op_id, operator_costs));
                    constraint.insert(op_id, operator_costs[op_id]);
                }
            }
            h_values_by_abstraction.push_back(
                abstraction->compute_goal_distances(operator_costs));
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
        "abstraction_generators",
        "abstraction generation methods",
        "[cartesian()]");

    parser.add_option<bool>(
        "saturated",
        "use saturated instead of full operator costs in constraints",
        "false");

    Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;

    return make_shared<PhOAbstractionConstraints>(opts);
}

static Plugin<ConstraintGenerator> _plugin("pho_abstraction_constraints", _parse);
}

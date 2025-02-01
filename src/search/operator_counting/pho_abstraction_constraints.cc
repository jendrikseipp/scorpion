#include "pho_abstraction_constraints.h"

#include "../cost_saturation/abstraction.h"
#include "../cost_saturation/abstraction_generator.h"
#include "../cost_saturation/utils.h"
#include "../lp/lp_solver.h"
#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"

#include <cassert>
#include <limits>

using namespace std;

namespace operator_counting {
PhOAbstractionConstraints::PhOAbstractionConstraints(const plugins::Options &opts)
    : abstraction_generators(
          opts.get_list<shared_ptr<cost_saturation::AbstractionGenerator>>(
              "abstractions")),
      saturated(opts.get<bool>("saturated")) {
}

void PhOAbstractionConstraints::initialize_constraints(
    const shared_ptr<AbstractTask> &task, lp::LinearProgram &lp) {
    cost_saturation::Abstractions abstractions =
        cost_saturation::generate_abstractions(task, abstraction_generators);
    abstraction_functions.reserve(abstractions.size());
    h_values_by_abstraction.reserve(abstractions.size());
    constraint_ids_by_abstraction.reserve(abstractions.size());

    vector<int> operator_costs = task_properties::get_operator_costs(TaskProxy(*task));
    int num_ops = operator_costs.size();
    int num_empty_constraints = 0;
    named_vector::NamedVector<lp::LPConstraint> &constraints = lp.get_constraints();

    if (saturated) {
        useless_operators.resize(num_ops, false);
        for (auto &abstraction : abstractions) {
            // Add constraint \sum_{o} Y_o * scf_h(o) >= 0.
            lp::LPConstraint constraint(0, lp.get_infinity());
            vector<int> h_values = abstraction->compute_goal_distances(
                operator_costs);
            vector<int> saturated_costs = abstraction->compute_saturated_costs(
                h_values);
            for (int op_id = 0; op_id < num_ops; ++op_id) {
                if (saturated_costs[op_id] != 0) {
                    if (saturated_costs[op_id] == -cost_saturation::INF) {
                        useless_operators[op_id] = true;
                    } else {
                        constraint.insert(op_id, saturated_costs[op_id]);
                    }
                }
            }
            if (constraint.empty()) {
                constraint_ids_by_abstraction.push_back(-1);
                ++num_empty_constraints;
            } else {
                constraint_ids_by_abstraction.push_back(constraints.size());
                constraints.push_back(move(constraint));
            }
            h_values_by_abstraction.push_back(move(h_values));
        }
    } else {
        for (auto &abstraction : abstractions) {
            lp::LPConstraint constraint(0, lp.get_infinity());
            for (int op_id = 0; op_id < num_ops; ++op_id) {
                if (abstraction->operator_is_active(op_id)) {
                    constraint.insert(op_id, operator_costs[op_id]);
                }
            }
            if (constraint.empty()) {
                ++num_empty_constraints;
                constraint_ids_by_abstraction.push_back(-1);
            } else {
                constraint_ids_by_abstraction.push_back(constraints.size());
                constraints.push_back(move(constraint));
            }
            h_values_by_abstraction.push_back(
                abstraction->compute_goal_distances(operator_costs));
        }
    }

    for (auto &abstraction : abstractions) {
        abstraction_functions.push_back(abstraction->extract_abstraction_function());
    }

    cout << "Empty constraints: " << num_empty_constraints << endl;
    cout << "Non-empty constraints: " << constraints.size() << endl;
}

bool PhOAbstractionConstraints::update_constraints(
    const State &state, lp::LPSolver &lp_solver) {
    if (!useless_operators.empty()) {
        int num_ops = useless_operators.size();
        // Force operator count of operators o with scf(o)=-\infty to be 0.
        for (int op_id = 0; op_id < num_ops; ++op_id) {
            if (useless_operators[op_id]) {
                lp_solver.set_variable_lower_bound(op_id, 0.0);
                lp_solver.set_variable_upper_bound(op_id, 0.0);
            }
        }
        // Only set variable bounds once.
        utils::release_vector_memory(useless_operators);
    }
    for (size_t i = 0; i < abstraction_functions.size(); ++i) {
        int state_id = abstraction_functions[i]->get_abstract_state_id(state);
        assert(utils::in_bounds(i, h_values_by_abstraction));
        const vector<int> &h_values = h_values_by_abstraction[i];
        assert(utils::in_bounds(state_id, h_values));
        int h = h_values[state_id];
        if (h == cost_saturation::INF) {
            return true;
        }
        if (constraint_ids_by_abstraction[i] != -1) {
            lp_solver.set_constraint_lower_bound(constraint_ids_by_abstraction[i], h);
        }
    }
    return false;
}

class PhOAbstractionConstraintsFeature
    : public plugins::TypedFeature<ConstraintGenerator, PhOAbstractionConstraints> {
public:
    PhOAbstractionConstraintsFeature() : TypedFeature("pho_abstraction_constraints") {
        document_title("(Saturated) posthoc optimization constraints for abstractions");

        add_list_option<shared_ptr<cost_saturation::AbstractionGenerator>>(
            "abstractions",
            "abstraction generation methods",
            plugins::ArgumentInfo::NO_DEFAULT);
        add_option<bool>(
            "saturated",
            "use saturated instead of full operator costs in constraints",
            "true");
    }
};

static plugins::FeaturePlugin<PhOAbstractionConstraintsFeature> _plugin;
}

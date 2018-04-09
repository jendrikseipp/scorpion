#include "pho_abstraction_constraints.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../cost_saturation/abstraction.h"
#include "../cost_saturation/abstraction_generator.h"
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
    const shared_ptr<AbstractTask> task,
    vector<lp::LPConstraint> &constraints,
    double infinity) {
    for (auto &abstraction_generator : abstraction_generators) {
        cost_saturation::Abstractions new_abstractions =
            abstraction_generator->generate_abstractions(task);
        abstractions.insert(
            abstractions.end(),
            make_move_iterator(new_abstractions.begin()),
            make_move_iterator(new_abstractions.end()));
    }
    vector<int> operator_costs = task_properties::get_operator_costs(TaskProxy(*task));
    constraint_offset = constraints.size();
    // TODO: Remove code duplication.
    if (saturated) {
        for (auto &abstraction : abstractions) {
            constraints.emplace_back(0, infinity);
            lp::LPConstraint &constraint = constraints.back();
            auto pair = abstraction->compute_goal_distances_and_saturated_costs(operator_costs);
            vector<int> &h_values = pair.first;
            vector<int> &saturated_costs = pair.second;
            for (int op_id : abstraction->get_active_operators()) {
                assert(utils::in_bounds(op_id, saturated_costs));
                constraint.insert(op_id, max(0, saturated_costs[op_id]));
            }
            h_values_by_abstraction.push_back(move(h_values));
            abstraction->release_transition_system_memory();
        }
    } else {
        for (auto &abstraction : abstractions) {
            constraints.emplace_back(0, infinity);
            lp::LPConstraint &constraint = constraints.back();
            for (int op_id : abstraction->get_active_operators()) {
                assert(utils::in_bounds(op_id, operator_costs));
                constraint.insert(op_id, operator_costs[op_id]);
            }
            h_values_by_abstraction.push_back(abstraction->compute_h_values(operator_costs));
            abstraction->release_transition_system_memory();
        }
    }
}

bool PhOAbstractionConstraints::update_constraints(
    const State &state, lp::LPSolver &lp_solver) {
    for (size_t i = 0; i < abstractions.size(); ++i) {
        int constraint_id = constraint_offset + i;
        const cost_saturation::Abstraction &abstraction = *abstractions[i];
        int state_id = abstraction.get_abstract_state_id(state);
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

static PluginShared<ConstraintGenerator> _plugin("pho_abstraction_constraints", _parse);
}

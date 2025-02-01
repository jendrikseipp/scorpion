#include "optimal_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "max_cost_partitioning_heuristic.h"
#include "utils.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/timer.h"

#include <cassert>
#include <cmath>

using namespace std;

namespace cost_saturation {
OptimalCostPartitioningHeuristic::OptimalCostPartitioningHeuristic(
    const vector<shared_ptr<AbstractionGenerator>> &abstraction_generators,
    bool allow_negative_costs, lp::LPSolverType lpsolver,
    const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description,
    utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      lp_solver(lpsolver),
      allow_negative_costs(allow_negative_costs) {
    utils::Timer timer;

    Abstractions abstractions = generate_abstractions(task, abstraction_generators);
    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    for (const auto &abstraction : abstractions) {
        h_values.push_back(abstraction->compute_goal_distances(costs));
    }

    generate_lp(abstractions);

    for (const auto &abstraction : abstractions) {
        abstraction_functions.push_back(abstraction->extract_abstraction_function());
    }

    cout << "LP construction time: " << timer << endl;
    lp_solver.print_statistics();

    // Cache indices for the last evaluated state to speed up adapting the LP.
    current_abstract_state_vars.resize(abstraction_functions.size());
    State initial_state = task_proxy.get_initial_state();
    for (int i = 0; i < static_cast<int>(abstraction_functions.size()); ++i) {
        int init_id = abstraction_functions[i]->get_abstract_state_id(initial_state);
        current_abstract_state_vars[i] = distance_variables[i][init_id];
    }

    release_memory();
}

void OptimalCostPartitioningHeuristic::release_memory() {
    utils::release_vector_memory(abstraction_variables);
    utils::release_vector_memory(operator_cost_variables);
}

int OptimalCostPartitioningHeuristic::compute_heuristic(const State &ancestor_state) {
    State concrete_state = convert_ancestor_state(ancestor_state);
    // Set upper bound for distance of current abstract states to 0 and for all other
    // abstract states to infinity.
    for (int id = 0; id < static_cast<int>(abstraction_functions.size()); ++id) {
        int new_state_id = abstraction_functions[id]->get_abstract_state_id(concrete_state);
        if (new_state_id == -1 || h_values[id][new_state_id] == INF) {
            return DEAD_END;
        }

        int old_state_var = current_abstract_state_vars[id];
        lp_solver.set_variable_upper_bound(old_state_var, lp_solver.get_infinity());
        if (allow_negative_costs) {
            lp_solver.set_variable_lower_bound(old_state_var, -lp_solver.get_infinity());
        }

        int new_state_var = distance_variables[id][new_state_id];
        lp_solver.set_variable_upper_bound(new_state_var, 0);
        if (allow_negative_costs) {
            lp_solver.set_variable_lower_bound(new_state_var, 0);
        }
        current_abstract_state_vars[id] = new_state_var;
    }

    lp_solver.solve();
    if (!lp_solver.has_optimal_solution()) {
        return DEAD_END;
    }

    double h_val = lp_solver.get_objective_value();
    double epsilon = 0.01;
    return static_cast<int>(ceil(h_val - epsilon));
}

void OptimalCostPartitioningHeuristic::generate_lp(const Abstractions &abstractions) {
    /*
      Build the following LP:

      Variables:
       * heuristic[A] for each abstraction A
       * distance[A][s'] for each abstraction A and each abstract state s' in A
       * operator_cost[A][o] for each abstraction A and each operator o

      Objective function: MAX sum_{A in abstractions} heuristic[A]

      Constraints:
       * For A in abstractions:
         * For <s', o, s''> in abstract transitions of abstraction A
             distance[A][s''] <= distance[A][s'] + operator_cost[A][o]
           Note that self-loops reduce to a special case that can
           be encoded in the variable bounds:
             operator_cost[A][o] >= 0
         * For each abstract goal state s' of abstraction A:
             heuristic[A] <= distance[A][s']
       * For o in operators:
             sum_{A in abstractions} operator_cost[A][o] <= cost(o)

      Lower bounds:
        If allow_negative_costs=true, all variables are unbounded,
        otherwise all are non-negative.

      Upper bounds:
       * heuristic[A] <= \infty
       * operator_cost[A][o] <= \infty (we could also use cost(o) but this
         information is already contained in the constraints)
       * (Only) the bounds for distance[A][s'] depend on the current state s
         and will be changed for every evaluation:
         * distance[A][s'] <= 0       if A maps s to s'
         * distance[A][s'] <= \infty  otherwise
    */
    LPVariables lp_variables;
    LPConstraints lp_constraints;
    for (int id = 0; id < static_cast<int>(abstractions.size()); ++id) {
        cout << "Add abstraction " << id + 1 << " of " << abstractions.size()
             << " to LP." << endl;
        const Abstraction &abstraction = *abstractions[id];
        add_abstraction_variables(abstraction, id, lp_variables);
        add_abstraction_constraints(abstraction, id, lp_constraints);
    }
    add_operator_cost_constraints(lp_constraints);
    lp::LinearProgram lp(
        lp::LPObjectiveSense::MAXIMIZE, move(lp_variables), move(lp_constraints),
        lp_solver.get_infinity());
    lp_solver.load_problem(lp);
}

void OptimalCostPartitioningHeuristic::add_abstraction_variables(
    const Abstraction &abstraction, int id, LPVariables &lp_variables) {
    assert(static_cast<int>(abstraction_variables.size()) == id);
    assert(static_cast<int>(distance_variables.size()) == id);
    assert(static_cast<int>(operator_cost_variables.size()) == id);

    double upper_bound = lp_solver.get_infinity();

    abstraction_variables.push_back(lp_variables.size());
    double default_lower_bound = allow_negative_costs ? -lp_solver.get_infinity() : 0.;
    lp_variables.emplace_back(default_lower_bound, upper_bound, 1.);

    int num_states = abstraction.get_num_states();
    distance_variables.emplace_back(num_states);
    for (int state_id = 0; state_id < num_states; ++state_id) {
        distance_variables[id][state_id] = lp_variables.size();
        lp_variables.emplace_back(default_lower_bound, upper_bound, 0.);
    }

    int num_operators = task_proxy.get_operators().size();
    operator_cost_variables.emplace_back(num_operators);
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        operator_cost_variables[id][op_id] = lp_variables.size();
        double lower_bound = abstraction.operator_induces_self_loop(op_id)
            ? 0.
            : default_lower_bound;
        lp_variables.emplace_back(lower_bound, upper_bound, 0.);
    }
}

void OptimalCostPartitioningHeuristic::add_abstraction_constraints(
    const Abstraction &abstraction, int id, LPConstraints &lp_constraints) {
    /*
      For <s', o, s''> in abstract transitions of abstraction A add constraint
      distance[A][s''] <= distance[A][s'] + operator_cost[A][o] which equals
      0 <= distance[A][s'] + operator_cost[A][o] - distance[A][s''] <= \infty
    */
    abstraction.for_each_transition(
        [this, id, &lp_constraints](const Transition &transition) {
            int from_col = distance_variables[id][transition.src];
            int op_col = operator_cost_variables[id][transition.op];
            int to_col = distance_variables[id][transition.target];
            lp::LPConstraint constraint(0., lp_solver.get_infinity());
            constraint.insert(from_col, 1);
            constraint.insert(op_col, 1);
            constraint.insert(to_col, -1);
            lp_constraints.push_back(move(constraint));
        });

    /*
      For each abstract goal state s' in abstraction A add constraint
      heuristic[A] <= distance[A][s'] which equals
      0 <= distance[A][s'] - heuristic[A] <= \infty
    */
    int abstraction_col = abstraction_variables[id];
    for (int goal_id : abstraction.get_goal_states()) {
        int goal_col = distance_variables[id][goal_id];
        lp::LPConstraint constraint(0., lp_solver.get_infinity());
        constraint.insert(goal_col, 1);
        constraint.insert(abstraction_col, -1);
        lp_constraints.push_back(move(constraint));
    }
}

void OptimalCostPartitioningHeuristic::add_operator_cost_constraints(
    LPConstraints &lp_constraints) {
    /*
      For o in operators add constraint
      sum_{A in abstractions} operator_cost[A][o] <= cost(o)
    */
    for (OperatorProxy op : task_proxy.get_operators()) {
        lp_constraints.emplace_back(-lp_solver.get_infinity(), op.get_cost());
        lp::LPConstraint &constraint = lp_constraints.back();
        for (size_t id = 0; id < operator_cost_variables.size(); ++id) {
            int abstraction_col = operator_cost_variables[id][op.get_id()];
            constraint.insert(abstraction_col, 1);
        }
    }
}

class OptimalCostPartitioningHeuristicFeature
    : public plugins::TypedFeature<Evaluator, OptimalCostPartitioningHeuristic> {
public:
    OptimalCostPartitioningHeuristicFeature() : TypedFeature("ocp") {
        document_subcategory("heuristics_cost_partitioning");
        document_title("Optimal cost partitioning heuristic");
        document_synopsis("Compute an optimal cost partitioning for each evaluated state.");
        add_options_for_cost_partitioning_heuristic(*this, "ocp");
        lp::add_lp_solver_option_to_feature(*this);
        add_option<bool>(
            "allow_negative_costs",
            "use general instead of non-negative cost partitioning",
            "true");
    }

    virtual shared_ptr<OptimalCostPartitioningHeuristic> create_component(
        const plugins::Options &options, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<OptimalCostPartitioningHeuristic>(
            options.get_list<shared_ptr<AbstractionGenerator>>("abstractions"),
            options.get<bool>("allow_negative_costs"),
            lp::get_lp_solver_arguments_from_options(options),
            get_heuristic_arguments_from_options(options));
    }
};

static plugins::FeaturePlugin<OptimalCostPartitioningHeuristicFeature> _plugin;
}

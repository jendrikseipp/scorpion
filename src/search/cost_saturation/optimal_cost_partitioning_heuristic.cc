#include "optimal_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "utils.h"

#include "../global_state.h"
#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/timer.h"

#include <cassert>
#include <cmath>

using namespace std;

namespace cost_saturation {
OptimalCostPartitioningHeuristic::OptimalCostPartitioningHeuristic(
    const options::Options &opts)
    : Heuristic(opts),
      abstractions(generate_abstractions(
                       task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"))),
      lp_solver(lp::LPSolverType(opts.get_enum("lpsolver"))),
      allow_negative_costs(true),
      debug(false) {
    utils::Timer timer;

    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    int num_operators = task_proxy.get_operators().size();
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        h_values.push_back(abstraction->compute_h_values(costs));
        looping_operators.push_back(
            convert_to_bitvector(abstraction->get_looping_operators(), num_operators));
    }

    generate_lp();
    cout << "LP construction time: " << timer << endl;
    lp_solver.print_statistics();

    timer.reset();

    lp_solver.solve();
    cout << "LP solving time: " << timer << endl;

    // Cache indices for the last evaluated state to speed-up adapting the LP.
    current_abstract_state_vars.resize(abstractions.size());
    State initial_state = task_proxy.get_initial_state();
    for (int id = 0; id < static_cast<int>(abstractions.size()); ++id) {
        int initial_state_index = abstractions[id]->get_abstract_state_id(initial_state);
        if (initial_state_index == -1) {
            current_abstract_state_vars[id] = -1;
        } else {
            current_abstract_state_vars[id] = distance_variables[id][initial_state_index];
        }
    }

    release_memory();
}

void OptimalCostPartitioningHeuristic::release_memory() {
    // Memory for the transition systems is released in generate_lp().
    utils::release_vector_memory(heuristic_variables);
    //utils::release_vector_memory(action_cost_variables);
}

int OptimalCostPartitioningHeuristic::compute_heuristic(const GlobalState &global_state) {
    State concrete_state = convert_global_state(global_state);
    // Set upper bound for distance of current abstract states to 0 and for all other
    // abstract states to infinity.
    for (int id = 0; id < static_cast<int>(abstractions.size()); ++id) {
        const Abstraction &abstraction = *abstractions[id];
        int new_state_id = abstraction.get_abstract_state_id(concrete_state);
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

    /*if (debug) {
        const double *sol = lp_solver->getColSolution();
        vector<double> solution(sol, sol + lp_solver->getNumCols());
        for (size_t abstraction_id = 0; abstraction_id < action_cost_variables.size(); ++abstraction_id) {
            vector<int> costs;
            for (int index : action_cost_variables[abstraction_id]) {
                costs.push_back(solution[index]);
            }
            cout << "c_" << abstraction_id << ": " << costs << endl;
        }
    }*/

    double h_val = lp_solver.get_objective_value();
    double epsilon = 0.01;
    return ceil(h_val - epsilon);
}

void OptimalCostPartitioningHeuristic::generate_lp() {
    // Build the following LP
    //
    // Variables:
    //  * heuristic[p] for each p in PDBs
    //  * distance[p][s'] for each p in PDBs and each s' in the abstract states of p
    //  * action_cost[p][a] for each p in PDBs and each action a
    //
    // Objective Function: MAX sum_{p in PDBs} heuristic[p]
    //
    // Constraints:
    //  * For p in PDBs
    //    * For <s', a, s''> in abstract transitions of PDB p
    //        distance[p][s''] <= distance[p][s'] + action_cost[p][a]
    //      Note that self-loops reduce to a special case that can
    //      be encoded in the variable bounds:
    //        action_cost[p][a] >= 0
    //    * For each abstract goal state s' of PDB p
    //        heuristic[p] <= distance[p][s']
    //  * For a in actions
    //        sum_{p in PDBs} action_cost[p][a] <= a.cost
    //
    // Lower bounds: if allow_negative_costs is set, all variables are unbounded,
    //               otherwise all are non-negative.
    // Upper bounds:
    //  * heuristic[p] <= \infty
    //  * action_cost[p][a] <= \infty (we could also use a.cost but this information
    //                                 is already contained in the constraints)
    //  * (Only) the bounds for distance[p][s'] depend on the current state s
    //    and will be changed for every evaluation
    //    * distance[p][s'] <= 0       if the abstraction of s in p is s'
    //    * distance[p][s'] <= \infty  otherwise
    vector<lp::LPVariable> lp_variables;
    vector<lp::LPConstraint> lp_constraints;
    for (int id = 0; id < static_cast<int>(abstractions.size()); ++id) {
        cout << "Add abstraction " << id + 1 << " of " << abstractions.size()
             << " to LP." << endl;
        Abstraction &abstraction = *abstractions[id];
        introduce_abstraction_variables(abstraction, id, lp_variables);
        add_abstraction_constraints(abstraction, id, lp_constraints);
        abstraction.release_transition_system_memory();
    }
    add_action_cost_constraints(lp_constraints);
    lp_solver.load_problem(lp::LPObjectiveSense::MAXIMIZE, lp_variables, lp_constraints);
}

void OptimalCostPartitioningHeuristic::introduce_abstraction_variables(
    const Abstraction &abstraction, int id, vector<lp::LPVariable> &lp_variables) {
    assert(static_cast<int>(heuristic_variables.size()) == id);
    assert(static_cast<int>(distance_variables.size()) == id);
    assert(static_cast<int>(action_cost_variables.size()) == id);

    double upper_bound = lp_solver.get_infinity();

    heuristic_variables.push_back(lp_variables.size());
    double default_lower_bound = allow_negative_costs ? -lp_solver.get_infinity() : 0.;
    lp_variables.emplace_back(default_lower_bound, upper_bound, 1.);

    int num_states = abstraction.get_num_states();
    distance_variables.emplace_back(num_states);
    for (int state_id = 0; state_id < num_states; ++state_id) {
        distance_variables[id][state_id] = lp_variables.size();
        lp_variables.emplace_back(default_lower_bound, upper_bound, 0.);
    }

    int num_operators = task_proxy.get_operators().size();
    action_cost_variables.emplace_back(num_operators);
    for (int op_id = 0; op_id < num_operators; ++op_id) {
        action_cost_variables[id][op_id] = lp_variables.size();
        double lower_bound = looping_operators[id][op_id] ? 0. : default_lower_bound;
        lp_variables.emplace_back(lower_bound, upper_bound, 0.);
    }
}

void OptimalCostPartitioningHeuristic::add_abstraction_constraints(
    const Abstraction &abstraction, int id,
    vector<lp::LPConstraint> &lp_constraints) {
    //    * For <s', a, s''> in abstract transitions of PDB p
    //        distance[p][s''] <= distance[p][s'] + action_cost[p][a] which equals
    //        0 <= distance[p][s'] + action_cost[p][a] - distance[p][s''] <= \infty
    for (const ExplicitTransition &transition : abstraction.get_transitions()) {
        int from_col = distance_variables[id][transition.src];
        int op_col = action_cost_variables[id][transition.op];
        int to_col = distance_variables[id][transition.target];
        lp::LPConstraint constraint(0., lp_solver.get_infinity());
        constraint.insert(from_col, 1);
        constraint.insert(op_col, 1);
        constraint.insert(to_col, -1);
        lp_constraints.push_back(move(constraint));
    }

    //    * For each abstract goal state s' of PDB p
    //        heuristic[p] <= distance[p][s'] which equals
    //        0 <= distance[p][s'] - heuristic[p] <= \infty
    int heuristic_col = heuristic_variables[id];
    for (int goal_id : abstraction.get_goal_states()) {
        int goal_col = distance_variables[id][goal_id];
        lp::LPConstraint constraint(0., lp_solver.get_infinity());
        constraint.insert(goal_col, 1);
        constraint.insert(heuristic_col, -1);
        lp_constraints.push_back(move(constraint));
    }
}

void OptimalCostPartitioningHeuristic::add_action_cost_constraints(
    vector<lp::LPConstraint> &lp_constraints) {
    //  * For a in actions
    //        0 <= sum_{p in PDBs} action_cost[p][a] <= a.cost
    for (OperatorProxy op : task_proxy.get_operators()) {
        lp_constraints.emplace_back(0., op.get_cost());
        lp::LPConstraint &constraint = lp_constraints.back();
        for (size_t id = 0; id < action_cost_variables.size(); ++id) {
            int abstraction_col = action_cost_variables[id][op.get_id()];
            constraint.insert(abstraction_col, 1);
        }

    }
}

static Heuristic *_parse(OptionParser &parser) {
    parser.document_synopsis(
        "Optimal cost partitioning heuristic",
        "");

    prepare_parser_for_cost_partitioning_heuristic(parser);
    lp::add_lp_solver_option_to_parser(parser);

    Options opts = parser.parse();
    if (parser.help_mode())
        return nullptr;

    if (parser.dry_run())
        return nullptr;

    return new OptimalCostPartitioningHeuristic(opts);
}

static Plugin<Heuristic> _plugin("optimal_cost_partitioning", _parse);
}

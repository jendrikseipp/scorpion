#include "optimal_cost_partitioning_heuristic.h"

#include "abstraction.h"
#include "cost_partitioning_heuristic.h"
#include "utils.h"

#include "../global_operator.h"
#include "../global_state.h"
#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/task_properties.h"
#include "../utils/collections.h"
#include "../utils/logging.h"
#include "../utils/timer.h"

#include <cassert>
#include <cmath>

#ifdef USE_LP
#ifdef __GNUG__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <OsiSolverInterface.hpp>
#include <CoinPackedMatrix.hpp>
#include <CoinPackedVector.hpp>
#ifdef __GNUG__
#pragma GCC diagnostic pop
#endif
#endif


using namespace std;

namespace cost_saturation {
OptimalCostPartitioningHeuristic::OptimalCostPartitioningHeuristic(
    const options::Options &opts)
    : Heuristic(opts),
      abstractions(generate_abstractions(
                       task, opts.get_list<shared_ptr<AbstractionGenerator>>("abstraction_generators"))),
      allow_negative_costs(true),
      debug(false) {
    int num_operators = task_proxy.get_operators().size();
    if (num_operators != static_cast<int>(g_operators.size())) {
        ABORT("OptimalCostPartitioningHeuristic doesn't work for task "
              "transformations that add or remove operators");
    }
    utils::Timer timer;

    vector<int> costs = task_properties::get_operator_costs(task_proxy);
    for (const unique_ptr<Abstraction> &abstraction : abstractions) {
        h_values.push_back(abstraction->compute_h_values(costs));
        looping_operators.push_back(
            convert_to_bitvector(abstraction->get_looping_operators(), num_operators));
    }

    lp_solver = lp::create_lp_solver(lp::LPSolverType(opts.get_enum("lpsolver")));
    lp_solver->messageHandler()->setLogLevel(0);
    generateLP();
    cout << "LP construction time: " << timer << endl;
    cout << "LP variables: " << variable_count << endl;
    cout << "LP constraints: " << constraint_count << endl;

    timer.reset();
    // After an initial solve we can always use resolve for solving a modified
    // version of the LP.
    lp_solver->initialSolve();
    cout << "LP initial solve time: " << timer << endl;
    current_abstract_state_vars.resize(abstractions.size());
    for (int id = 0; id < static_cast<int>(abstractions.size()); ++id) {
        int initial_state_index = abstractions[id]->get_abstract_state_id(
            task_proxy.get_initial_state());
        if (initial_state_index == -1) {
            current_abstract_state_vars[id] = -1;
        } else {
            current_abstract_state_vars[id] = distance_variables[id][initial_state_index];
        }
    }
    release_memory();
}

OptimalCostPartitioningHeuristic::~OptimalCostPartitioningHeuristic() {
}

void OptimalCostPartitioningHeuristic::release_memory() {
    // Memory for the transition systems is released in generateLP().
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
        lp_solver->setColUpper(old_state_var, lp_solver->getInfinity());
        if (allow_negative_costs)
            lp_solver->setColLower(old_state_var, -lp_solver->getInfinity());
        int new_state_var = distance_variables[id][new_state_id];
        lp_solver->setColUpper(new_state_var, 0);
        if (allow_negative_costs) {
            lp_solver->setColLower(new_state_var, 0);
        }
        current_abstract_state_vars[id] = new_state_var;
    }
    lp_solver->resolve();
    if (lp_solver->isProvenDualInfeasible()) {
        return DEAD_END;
    }

    if (debug) {
        const double *sol = lp_solver->getColSolution();
        vector<double> solution(sol, sol + lp_solver->getNumCols());
        for (size_t abstraction_id = 0; abstraction_id < action_cost_variables.size(); ++abstraction_id) {
            vector<int> costs;
            for (int index : action_cost_variables[abstraction_id]) {
                costs.push_back(solution[index]);
            }
            cout << "c_" << abstraction_id << ": " << costs << endl;
        }
    }

    double h_val = lp_solver->getObjValue();
    double epsilon = 0.01;
    return ceil(h_val - epsilon);
}

void OptimalCostPartitioningHeuristic::generateLP() {
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
    vector<MatrixEntry> matrix_entries;
    vector<double> constraint_upper_bounds;
    // No need to store lower bounds (they are all 0)
    vector<double> variable_lower_bounds;
    // No need to store upper bounds (they are all +inf)
    constraint_count = 0;
    variable_count = 0;

    for (int id = 0; id < static_cast<int>(abstractions.size()); ++id) {
        cout << "Add abstraction " << id + 1 << " of " << abstractions.size()
             << " to LP" << endl;
        Abstraction &abstraction = *abstractions[id];
        introduce_abstraction_variables(abstraction, id, variable_lower_bounds);
        add_abstraction_constraints(abstraction, id, matrix_entries, constraint_upper_bounds);
        abstraction.release_transition_system_memory();
    }
    add_action_cost_constraints(matrix_entries, constraint_upper_bounds);

    int matrix_entry_count = matrix_entries.size();
    cout << "Non-zero matrix entries: " << matrix_entry_count << endl;
    int *rowIndices = new int[matrix_entry_count];
    int *colIndices = new int[matrix_entry_count];
    double *elements = new double[matrix_entry_count];
    for (int i = 0; i < matrix_entry_count; ++i) {
        rowIndices[i] = matrix_entries[i].row;
        colIndices[i] = matrix_entries[i].col;
        elements[i] = matrix_entries[i].element;
    }
    CoinPackedMatrix matrix(false, rowIndices, colIndices, elements, matrix_entry_count);

    int num_rows = constraint_count;
    int num_cols = variable_count;

    // Maxmize: sum heuristic_p
    double *objective = new double[num_cols];    //objective coefficients
    fill(objective, objective + num_cols, 0);
    for (size_t pdb_id = 0; pdb_id < heuristic_variables.size(); ++pdb_id) {
        objective[heuristic_variables[pdb_id]] = 1;
    }
    lp_solver->setObjSense(-1);

    // lower/upper bounds
    double *col_lb = new double[num_cols];       //column lower bounds
    for (size_t i = 0; i < variable_lower_bounds.size(); ++i) {
        col_lb[i] = variable_lower_bounds[i];
    }
    double *col_ub = new double[num_cols];       //column upper bounds
    fill(col_ub, col_ub + num_cols, lp_solver->getInfinity());
    double *row_lb = new double[num_rows]; //the row lower bounds
    fill(row_lb, row_lb + num_rows, 0);
    double *row_ub = new double[num_rows]; //the row upper bounds
    for (size_t i = 0; i < constraint_upper_bounds.size(); ++i) {
        row_ub[i] = constraint_upper_bounds[i];
    }

    lp_solver->loadProblem(matrix, col_lb, col_ub, objective,
                           row_lb, row_ub);
    // Clean up
    delete[] rowIndices;
    delete[] colIndices;
    delete[] elements;
    delete[] objective;
    delete[] col_lb;
    delete[] col_ub;
    delete[] row_ub;
    delete[] row_lb;
}

void OptimalCostPartitioningHeuristic::introduce_abstraction_variables(
    const Abstraction &abstraction, int id, vector<double> &variable_lower_bounds) {
    assert(heuristic_variables.size() == static_cast<size_t>(id));
    assert(distance_variables.size() == static_cast<size_t>(id));
    assert(action_cost_variables.size() == static_cast<size_t>(id));
    heuristic_variables.push_back(variable_count++);
    if (allow_negative_costs) {
        variable_lower_bounds.push_back(-lp_solver->getInfinity());
    } else {
        variable_lower_bounds.push_back(0);
    }
    size_t abstract_state_count = abstraction.get_num_states();
    distance_variables.push_back(vector<int>(abstract_state_count));
    for (size_t state_id = 0; state_id < abstract_state_count; ++state_id) {
        distance_variables[id][state_id] = variable_count++;
        if (allow_negative_costs) {
            variable_lower_bounds.push_back(-lp_solver->getInfinity());
        } else {
            variable_lower_bounds.push_back(0);
        }
    }
    action_cost_variables.push_back(vector<int>(g_operators.size()));
    for (size_t op_id = 0; op_id < g_operators.size(); ++op_id) {
        action_cost_variables[id][op_id] = variable_count++;
        if (allow_negative_costs && !looping_operators[id][op_id]) {
            variable_lower_bounds.push_back(-lp_solver->getInfinity());
        } else {
            variable_lower_bounds.push_back(0);
        }
    }
}

void OptimalCostPartitioningHeuristic::add_abstraction_constraints(
    const Abstraction &abstraction, int id,
    vector<MatrixEntry> &matrix_entries, vector<double> &constraint_upper_bounds) {
    //    * For <s', a, s''> in abstract transitions of PDB p
    //        distance[p][s''] <= distance[p][s'] + action_cost[p][a]
    //        0 <= distance[p][s'] + action_cost[p][a] - distance[p][s''] <= \infty
    for (const ExplicitTransition &transition : abstraction.get_transitions()) {
        int row = constraint_count++;
        int from_col = distance_variables[id][transition.src];
        int op_col = action_cost_variables[id][transition.op];
        int to_col = distance_variables[id][transition.target];
        matrix_entries.push_back(MatrixEntry(row, from_col, 1));
        matrix_entries.push_back(MatrixEntry(row, op_col, 1));
        matrix_entries.push_back(MatrixEntry(row, to_col, -1));
        constraint_upper_bounds.push_back(lp_solver->getInfinity());
    }

    //    * For each abstract goal state s' of PDB p
    //        heuristic[p] <= distance[p][s']
    //        0 <= distance[p][s'] - heuristic[p] <= \infty
    int heuristic_col = heuristic_variables[id];
    for (int goal_id : abstraction.get_goal_states()) {
        int row = constraint_count++;
        int goal_col = distance_variables[id][goal_id];
        matrix_entries.push_back(MatrixEntry(row, goal_col, 1));
        matrix_entries.push_back(MatrixEntry(row, heuristic_col, -1));
        constraint_upper_bounds.push_back(lp_solver->getInfinity());
    }
}

void OptimalCostPartitioningHeuristic::add_action_cost_constraints(vector<MatrixEntry> &matrix_entries,
                                                                   vector<double> &constraint_upper_bounds) {
    //  * For a in actions
    //        0 <= sum_{p in PDBs} action_cost[p][a] <= a.cost
    for (size_t op_id = 0; op_id < g_operators.size(); ++op_id) {
        int row = constraint_count++;
        for (size_t id = 0; id < action_cost_variables.size(); ++id) {
            int abstraction_col = action_cost_variables[id][op_id];
            matrix_entries.push_back(MatrixEntry(row, abstraction_col, 1));
        }
        constraint_upper_bounds.push_back(g_operators[op_id].get_cost());
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

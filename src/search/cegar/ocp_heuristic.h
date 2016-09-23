#ifndef CEGAR_OCP_HEURISTIC_H
#define CEGAR_OCP_HEURISTIC_H

#include "transition_system.h"

#include "../heuristic.h"

#include "../lp/lp_internals.h"
#include "../lp/lp_solver.h"

#include <memory>
#include <vector>

#ifdef USE_LP
#include "CoinPackedVector.hpp"
#include "CoinPackedMatrix.hpp"
#include <sys/times.h>
#endif

namespace cegar {
class OptimalCostPartitioningHeuristic : public Heuristic {
    class MatrixEntry {
public:
        int row;
        int col;
        double element;
        MatrixEntry(int row_, int col_, double element_)
            : row(row_), col(col_), element(element_) {
        }
    };

    std::vector<std::shared_ptr<TransitionSystem>> abstractions;
    bool allow_negative_costs;
#ifdef USE_LP
    std::unique_ptr<OsiSolverInterface> lp_solver;
#endif
    // Column indices for heuristic variables indexed by PDB id.
    // The variable with id heuristic_variables[p] encodes the shortest distance
    // of the current abstract state to its nearest abstract goal state in pdb p
    // using the cost partitioning.
    std::vector<int> heuristic_variables;
    // Column indices for distance variables indexed by PDB id and abstract state id.
    // The variable with id distance_variables[p][s] encodes the distance of abstract
    // state s in pdb p from the current abstract state using the cost partitioning.
    std::vector<std::vector<int>> distance_variables;
    // Column indices for action cost variables indexed by PDB id and operator id.
    // The variable with id action_cost_variables[p][a] encodes cost action a should
    // have in pdb p.
    std::vector<std::vector<int>> action_cost_variables;

    int variable_count;
    int constraint_count;

    // Cache the variables corresponding to the current state in all pdbs.
    // This makes it easier to reset the bounds in each step.
    std::vector<int> current_abstract_state_vars;

    bool debug;

    void generateLP();
    void introduce_abstraction_variables(const TransitionSystem &abstraction,
                                         int abstraction_id,
                                         std::vector<double> &variable_lower_bounds);
    void add_abstraction_constraints(const TransitionSystem &abstraction,
                                     int abstraction_id,
                                     std::vector<MatrixEntry> &matrix_entries,
                                     std::vector<double> &constraint_upper_bounds);
    void add_action_cost_constraints(std::vector<MatrixEntry> &matrix_entries,
                                     std::vector<double> &constraint_upper_bounds);
    void release_memory();
protected:
    virtual int compute_heuristic(const GlobalState &global_state);
public:
    OptimalCostPartitioningHeuristic(
        const options::Options &opts,
        const std::vector<std::shared_ptr<TransitionSystem>> &&abstractions);
    ~OptimalCostPartitioningHeuristic();
};
}

#endif

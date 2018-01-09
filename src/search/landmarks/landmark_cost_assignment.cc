#include "landmark_cost_assignment.h"

#include "landmark_graph.h"
#include "util.h"

#include "../algorithms/max_cliques.h"
#include "../cost_saturation/scoring_functions.h"
#include "../cost_saturation/types.h"
#include "../utils/collections.h"
#include "../utils/language.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>

using namespace std;
using cost_saturation::ScoringFunction;

namespace landmarks {
LandmarkCostAssignment::LandmarkCostAssignment(const vector<int> &operator_costs,
                                               const LandmarkGraph &graph)
    : lm_graph(graph), operator_costs(operator_costs) {
}

const set<int> &LandmarkCostAssignment::get_achievers(
    int lmn_status, const LandmarkNode &lmn) const {
    // Return relevant achievers of the landmark according to its status.
    if (lmn_status == lm_not_reached)
        return lmn.first_achievers;
    else if (lmn_status == lm_needed_again)
        return lmn.possible_achievers;
    else
        return empty;
}


static vector<double> convert_to_double(const vector<int> &int_vec) {
    vector<double> double_vec;
    double_vec.reserve(int_vec.size());
    for (int value : int_vec) {
        double_vec.push_back(value);
    }
    return double_vec;
}


// Uniform cost partioning
LandmarkUniformSharedCostAssignment::LandmarkUniformSharedCostAssignment(
    const vector<int> &operator_costs,
    const LandmarkGraph &graph,
    bool use_action_landmarks,
    bool reuse_costs,
    bool greedy,
    enum cost_saturation::ScoringFunction scoring_function,
    const shared_ptr<utils::RandomNumberGenerator> &rng)
    : LandmarkCostAssignment(operator_costs, graph),
      use_action_landmarks(use_action_landmarks),
      reuse_costs(reuse_costs),
      greedy(greedy),
      scoring_function(scoring_function),
      rng(rng),
      original_costs(convert_to_double(operator_costs)) {
}

vector<int> LandmarkUniformSharedCostAssignment::compute_landmark_order(
    const vector<vector<int>> &achievers_by_lm) {
    vector<int> order(achievers_by_lm.size());
    iota(order.begin(), order.end(), 0);
    if (scoring_function == ScoringFunction::RANDOM) {
        rng->shuffle(order);
        return order;
    }

    // Compute h-values and saturated costs for each landmark.
    vector<int> h_values;
    h_values.reserve(achievers_by_lm.size());
    vector<int> used_costs;
    used_costs.reserve(achievers_by_lm.size());
    for (const vector<int> &achievers : achievers_by_lm) {
        int min_cost = numeric_limits<int>::max();
        for (int op_id : achievers) {
            assert(utils::in_bounds(op_id, operator_costs));
            min_cost = min(min_cost, operator_costs[op_id]);
        }
        h_values.push_back(min_cost);
        used_costs.push_back(min_cost * achievers.size());
    }
    assert(h_values.size() == achievers_by_lm.size());
    assert(used_costs.size() == achievers_by_lm.size());

    if (scoring_function == ScoringFunction::MIN_STOLEN_COSTS ||
        scoring_function == ScoringFunction::MAX_HEURISTIC_PER_STOLEN_COSTS) {
        vector<int> surplus_costs = operator_costs;
        for (size_t i = 0; i < achievers_by_lm.size(); ++i) {
            const vector<int> &achievers = achievers_by_lm[i];
            for (int op_id : achievers) {
                surplus_costs[op_id] -= h_values[i];
            }
        }
        used_costs.clear();
        int i = 0;
        for (const vector<int> &achievers : achievers_by_lm) {
            int wanted_by_lm = h_values[i];
            int stolen = 0;
            for (int op_id : achievers) {
                stolen += cost_saturation::compute_stolen_costs(
                    wanted_by_lm, surplus_costs[op_id]);
            }
            used_costs.push_back(stolen);
            ++i;
        }
        assert(used_costs.size() == achievers_by_lm.size());
    }

    vector<double> scores;
    scores.reserve(achievers_by_lm.size());
    for (size_t i = 0; i < achievers_by_lm.size(); ++i) {
        scores.push_back(cost_saturation::compute_score(
            h_values[i], used_costs[i], scoring_function, false));
    }
    sort(order.begin(), order.end(), [&](int i, int j) {
            return scores[i] > scores[j];
        });

    if (false) {
        cout << "h-values: " << h_values << endl;
        cout << "order: " << order << endl;
    }
    return order;
}

double LandmarkUniformSharedCostAssignment::cost_sharing_h_value() {
    vector<int> achieved_lms_by_op(operator_costs.size(), 0);
    vector<bool> action_landmarks(operator_costs.size(), false);

    const set<LandmarkNode *> &nodes = lm_graph.get_nodes();

    double h = 0;

    /* First pass:
       compute which op achieves how many landmarks. Along the way,
       mark action landmarks and add their cost to h. */
    for (const LandmarkNode *node : nodes) {
        int lmn_status = node->get_status();
        if (lmn_status != lm_reached) {
            const set<int> &achievers = get_achievers(lmn_status, *node);
            assert(!achievers.empty());
            if (use_action_landmarks && achievers.size() == 1) {
                // We have found an action landmark for this state.
                int op_id = *achievers.begin();
                if (!action_landmarks[op_id]) {
                    action_landmarks[op_id] = true;
                    assert(utils::in_bounds(op_id, operator_costs));
                    h += operator_costs[op_id];
                }
            } else {
                for (int op_id : achievers) {
                    assert(utils::in_bounds(op_id, achieved_lms_by_op));
                    ++achieved_lms_by_op[op_id];
                }
            }
        }
    }

    vector<const LandmarkNode *> relevant_lms;

    /* Second pass:
       remove landmarks from consideration that are covered by
       an action landmark; decrease the counters accordingly
       so that no unnecessary cost is assigned to these landmarks. */
    for (const LandmarkNode *node : nodes) {
        int lmn_status = node->get_status();
        if (lmn_status != lm_reached) {
            const set<int> &achievers = get_achievers(lmn_status, *node);
            bool covered_by_action_lm = false;
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, action_landmarks));
                if (action_landmarks[op_id]) {
                    covered_by_action_lm = true;
                    break;
                }
            }
            if (covered_by_action_lm) {
                for (int op_id : achievers) {
                    assert(utils::in_bounds(op_id, achieved_lms_by_op));
                    --achieved_lms_by_op[op_id];
                }
            } else {
                relevant_lms.push_back(node);
            }
        }
    }

    /* Third pass:
       count shared costs for the remaining landmarks. */
    if (reuse_costs || greedy) {
        // UOCP + ZOCP + SCP
        remaining_costs = original_costs;
        vector<vector<int>> achievers_by_lm;
        achievers_by_lm.reserve(relevant_lms.size());
        for (const LandmarkNode *node : relevant_lms) {
            int lmn_status = node->get_status();
            const set<int> &achievers = get_achievers(lmn_status, *node);
            achievers_by_lm.emplace_back(achievers.begin(), achievers.end());
        }
        for (int lm_id : compute_landmark_order(achievers_by_lm)) {
            const vector<int> &achievers = achievers_by_lm[lm_id];
            double min_cost = numeric_limits<double>::max();
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, achieved_lms_by_op));
                int num_achieved = achieved_lms_by_op[op_id];
                assert(num_achieved >= 1);
                assert(utils::in_bounds(op_id, remaining_costs));
                double cost = greedy ? remaining_costs[op_id] :
                              remaining_costs[op_id] / num_achieved;
                min_cost = min(min_cost, cost);
            }
            h += min_cost;
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, remaining_costs));
                double &remaining_cost = remaining_costs[op_id];
                assert(remaining_cost >= 0);
                if (reuse_costs) {
                    remaining_cost -= min_cost;
                } else {
                    remaining_cost = 0.0;
                }
                assert(remaining_cost >= 0);
                --achieved_lms_by_op[op_id];
            }
        }
    } else {
        // UCP
        for (const LandmarkNode *node : relevant_lms) {
            int lmn_status = node->get_status();
            const set<int> &achievers = get_achievers(lmn_status, *node);
            double min_cost = numeric_limits<double>::max();
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, achieved_lms_by_op));
                int num_achieved = achieved_lms_by_op[op_id];
                assert(num_achieved >= 1);
                assert(utils::in_bounds(op_id, operator_costs));
                double shared_cost = static_cast<double>(operator_costs[op_id]) / num_achieved;
                min_cost = min(min_cost, shared_cost);
            }
            h += min_cost;
        }
    }

    return h;
}


LandmarkCanonicalHeuristic::LandmarkCanonicalHeuristic(
    const vector<int> &operator_costs,
    const LandmarkGraph &graph)
    : LandmarkCostAssignment(operator_costs, graph) {
}

static bool empty_intersection(const set<int> &x, const set<int> &y) {
    set<int>::const_iterator i = x.begin();
    set<int>::const_iterator j = y.begin();
    while (i != x.end() && j != y.end()) {
        if (*i == *j)
            return false;
        else if (*i < *j)
            ++i;
        else
            ++j;
    }
    return true;
}

vector<vector<int>> LandmarkCanonicalHeuristic::compute_max_additive_subsets(
    const vector<const LandmarkNode *> &relevant_landmarks) {
    int num_landmarks = relevant_landmarks.size();

    // Initialize compatibility graph.
    vector<vector<int>> cgraph;
    cgraph.resize(num_landmarks);

    for (int i = 0; i < num_landmarks; ++i) {
        const LandmarkNode *lm1 = relevant_landmarks[i];
        const set<int> &achievers1 = get_achievers(lm1->get_status(), *lm1);
        for (int j = i + 1; j < num_landmarks; ++j) {
            const LandmarkNode *lm2 = relevant_landmarks[j];
            const set<int> &achievers2 = get_achievers(lm2->get_status(), *lm2);
            if (empty_intersection(achievers1, achievers2)) {
                /* If the two landmarks are additive, there is an edge in the
                   compatibility graph. */
                cgraph[i].push_back(j);
                cgraph[j].push_back(i);
            }
        }
    }

    vector<vector<int>> max_cliques;
    max_cliques::compute_max_cliques(cgraph, max_cliques);
    return max_cliques;
}

int LandmarkCanonicalHeuristic::compute_minimum_landmark_cost(const LandmarkNode &lm) const {
    int lm_status = lm.get_status();
    const set<int> &achievers = get_achievers(lm_status, lm);
    assert(!achievers.empty());
    int min_cost = numeric_limits<int>::max();
    for (int op_id : achievers) {
        assert(utils::in_bounds(op_id, operator_costs));
        min_cost = min(min_cost, operator_costs[op_id]);
    }
    return min_cost;
}

double LandmarkCanonicalHeuristic::cost_sharing_h_value() {
    // Ignore reached landmarks.
    vector<const LandmarkNode *> relevant_landmarks;
    for (const LandmarkNode *node : lm_graph.get_nodes()) {
        if (node->get_status() != lm_reached) {
            relevant_landmarks.push_back(node);
        }
    }

    vector<vector<int>> max_additive_subsets = compute_max_additive_subsets(relevant_landmarks);

    vector<int> minimum_landmark_costs;
    minimum_landmark_costs.reserve(relevant_landmarks.size());
    for (const LandmarkNode *node : relevant_landmarks) {
        minimum_landmark_costs.push_back(compute_minimum_landmark_cost(*node));
    }

    int max_h = 0;
    for (const vector<int> &additive_subset : max_additive_subsets) {
        int sum_h = 0;
        for (int landmark_id : additive_subset) {
            assert(utils::in_bounds(landmark_id, minimum_landmark_costs));
            int h = minimum_landmark_costs[landmark_id];
            sum_h += h;
        }
        max_h = max(max_h, sum_h);
    }
    assert(max_h >= 0);

    return max_h;
}


LandmarkPhO::LandmarkPhO(
    const vector<int> &operator_costs,
    const LandmarkGraph &graph,
    lp::LPSolverType solver_type)
    : LandmarkCostAssignment(operator_costs, graph),
      lp_solver(solver_type) {
    /* The LP has one variable (column) per landmark and one
       inequality (row) per operator. */
    int num_cols = lm_graph.number_of_landmarks();
    int num_rows = operator_costs.size();

    /* We want to maximize \sum_i w_i * cost(lm_i) * [lm_i not achieved],
       where cost(lm_i) is the cost of the cheapest operator achieving lm_i.
       We adapt the variable bounds in each state to ignore achieved landmarks
       and initialize the range to [0.0, 0.0]. */
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode *lm = lm_graph.get_lm_for_index(lm_id);
        int min_cost = compute_minimum_landmark_cost(*lm);
        lp_variables.emplace_back(0.0, 0.0, min_cost);
    }

    /*
      Set the constraint bounds. The constraints for operator o are of the form
      w_1 + w_5 + ... + w_k <= 1
      where w_1, w_5, ..., w_k are the weights for the landmarks for which o is
      a relevant achiever.
    */
    lp_constraints.resize(num_rows, lp::LPConstraint(0.0, 0.0));
    for (size_t op_id = 0; op_id < operator_costs.size(); ++op_id) {
        lp_constraints[op_id].set_lower_bound(-lp_solver.get_infinity());
        lp_constraints[op_id].set_upper_bound(1);
    }
}

int LandmarkPhO::compute_minimum_landmark_cost(const LandmarkNode &lm) const {
    int lm_status = lm.get_status();
    const set<int> &achievers = get_achievers(lm_status, lm);
    assert(!achievers.empty());
    int min_cost = numeric_limits<int>::max();
    for (int op_id : achievers) {
        assert(utils::in_bounds(op_id, operator_costs));
        min_cost = min(min_cost, operator_costs[op_id]);
    }
    return min_cost;
}

double LandmarkPhO::cost_sharing_h_value() {
    /* TODO: We could also do the same thing with action landmarks we
             do in the uniform cost partitioning case. */

    /*
      Set up LP variable bounds for the landmarks.
      The range of w_i is {0} if the corresponding landmark is already
      reached; otherwise it is [0, infinity].
      The lower bounds are set to 0 in the constructor and never change.
    */
    int num_cols = lm_graph.number_of_landmarks();
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode *lm = lm_graph.get_lm_for_index(lm_id);
        if (lm->get_status() == lm_reached) {
            lp_variables[lm_id].upper_bound = 0;
        } else {
            lp_variables[lm_id].upper_bound = lp_solver.get_infinity();
        }
    }

    /*
      Define the constraint matrix. The constraints for operator o are of the form
      w_1 + w_5 + ... + w_k <= 1
      where w_1, w_5, ..., w_k are the weights for the landmarks for which o is
      a relevant achiever. Hence, we add a triple (op, lm, 1.0)
      for each relevant achiever op of landmark lm, denoting that
      in the op-th row and lm-th column, the matrix has a 1.0 entry.
    */
    // Reuse previous constraint objects to save the effort of recreating them.
    for (lp::LPConstraint &constraint : lp_constraints) {
        constraint.clear();
    }
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode *lm = lm_graph.get_lm_for_index(lm_id);
        int lm_status = lm->get_status();
        if (lm_status != lm_reached) {
            const set<int> &achievers = get_achievers(lm_status, *lm);
            assert(!achievers.empty());
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, lp_constraints));
                lp_constraints[op_id].insert(lm_id, 1.0);
            }
        }
    }

    /* Copy non-empty constraints and use those in the LP.
       This significantly speeds up the heuristic calculation. See comment for OCP. */
    non_empty_lp_constraints.clear();
    for (const lp::LPConstraint &constraint : lp_constraints) {
        if (!constraint.empty())
            non_empty_lp_constraints.push_back(constraint);
    }

    // Load the problem into the LP solver.
    lp_solver.load_problem(
        lp::LPObjectiveSense::MAXIMIZE, lp_variables, non_empty_lp_constraints);

    // Solve the linear program.
    lp_solver.solve();

    assert(lp_solver.has_optimal_solution());
    double h = lp_solver.get_objective_value();

    return h;
}

LandmarkEfficientOptimalSharedCostAssignment::LandmarkEfficientOptimalSharedCostAssignment(
    const vector<int> &operator_costs,
    const LandmarkGraph &graph,
    lp::LPSolverType solver_type)
    : LandmarkCostAssignment(operator_costs, graph),
      lp_solver(solver_type) {
    /* The LP has one variable (column) per landmark and one
       inequality (row) per operator. */
    int num_cols = lm_graph.number_of_landmarks();
    int num_rows = operator_costs.size();

    /* We want to maximize 1 * cost(lm_1) + ... + 1 * cost(lm_n),
       so the coefficients are all 1.
       Variable bounds are state-dependent; we initialize the range to {0}. */
    lp_variables.resize(num_cols, lp::LPVariable(0.0, 0.0, 1.0));

    /* Set up lower bounds and upper bounds for the inequalities.
       These simply say that the operator's total cost must fall
       between 0 and the real operator cost. */
    lp_constraints.resize(num_rows, lp::LPConstraint(0.0, 0.0));
    for (size_t op_id = 0; op_id < operator_costs.size(); ++op_id) {
        lp_constraints[op_id].set_lower_bound(0);
        lp_constraints[op_id].set_upper_bound(operator_costs[op_id]);
    }
}


double LandmarkEfficientOptimalSharedCostAssignment::cost_sharing_h_value() {
    /* TODO: We could also do the same thing with action landmarks we
             do in the uniform cost partitioning case. */

    /*
      Set up LP variable bounds for the landmarks.
      The range of cost(lm_1) is {0} if the landmark is already
      reached; otherwise it is [0, infinity].
      The lower bounds are set to 0 in the constructor and never change.
    */
    int num_cols = lm_graph.number_of_landmarks();
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode *lm = lm_graph.get_lm_for_index(lm_id);
        if (lm->get_status() == lm_reached) {
            lp_variables[lm_id].upper_bound = 0;
        } else {
            lp_variables[lm_id].upper_bound = lp_solver.get_infinity();
        }
    }

    /*
      Define the constraint matrix. The constraints are of the form
      cost(lm_i1) + cost(lm_i2) + ... + cost(lm_in) <= cost(o)
      where lm_i1 ... lm_in are the landmarks for which o is a
      relevant achiever. Hence, we add a triple (op, lm, 1.0)
      for each relevant achiever op of landmark lm, denoting that
      in the op-th row and lm-th column, the matrix has a 1.0 entry.
    */
    // Reuse previous constraint objects to save the effort of recreating them.
    for (lp::LPConstraint &constraint : lp_constraints) {
        constraint.clear();
    }
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode *lm = lm_graph.get_lm_for_index(lm_id);
        int lm_status = lm->get_status();
        if (lm_status != lm_reached) {
            const set<int> &achievers = get_achievers(lm_status, *lm);
            assert(!achievers.empty());
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, lp_constraints));
                lp_constraints[op_id].insert(lm_id, 1.0);
            }
        }
    }

    /* Copy non-empty constraints and use those in the LP.
       This significantly speeds up the heuristic calculation. See issue443. */
    // TODO: do not copy the data here.
    non_empty_lp_constraints.clear();
    for (const lp::LPConstraint &constraint : lp_constraints) {
        if (!constraint.empty())
            non_empty_lp_constraints.push_back(constraint);
    }

    // Load the problem into the LP solver.
    lp_solver.load_problem(lp::LPObjectiveSense::MAXIMIZE,
                           lp_variables, non_empty_lp_constraints);

    // Solve the linear program.
    lp_solver.solve();

    assert(lp_solver.has_optimal_solution());
    double h = lp_solver.get_objective_value();

    return h;
}
}

#include "landmark_cost_partitioning_algorithms.h"

#include "landmark.h"
#include "landmark_graph.h"
#include "landmark_status_manager.h"
#include "util.h"

#include "../algorithms/max_cliques.h"
#include "../cost_saturation/greedy_order_utils.h"
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
#include <numeric>

using namespace std;
using cost_saturation::ScoringFunction;

namespace landmarks {
CostPartitioningAlgorithm::CostPartitioningAlgorithm(
    const vector<int> &operator_costs, const LandmarkGraph &graph)
    : lm_graph(graph), operator_costs(operator_costs) {
}

const Achievers &CostPartitioningAlgorithm::get_achievers(
    const Landmark &landmark, bool past) const {
    // Return relevant achievers of the landmark according to its status.
    if (past) {
        return landmark.possible_achievers;
    } else {
        return landmark.first_achievers;
    }
}


static vector<double> convert_to_double(const vector<int> &int_vec) {
    vector<double> double_vec(int_vec.begin(), int_vec.end());
    return double_vec;
}


UniformCostPartitioningAlgorithm::UniformCostPartitioningAlgorithm(
    const vector<int> &operator_costs,
    const LandmarkGraph &graph,
    bool use_action_landmarks,
    bool reuse_costs,
    bool greedy,
    enum cost_saturation::ScoringFunction scoring_function,
    const shared_ptr<utils::RandomNumberGenerator> &rng)
    : CostPartitioningAlgorithm(operator_costs, graph),
      use_action_landmarks(use_action_landmarks),
      reuse_costs(reuse_costs),
      greedy(greedy),
      scoring_function(scoring_function),
      rng(rng),
      original_costs(convert_to_double(operator_costs)) {
}

vector<int> UniformCostPartitioningAlgorithm::compute_landmark_order(
    const vector<vector<int>> &achievers_by_lm) {
    vector<int> order(achievers_by_lm.size());
    iota(order.begin(), order.end(), 0);

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
                             h_values[i], used_costs[i], scoring_function));
    }
    sort(order.begin(), order.end(), [&](int i, int j) {
             return scores[i] > scores[j];
         });

    return order;
}

double UniformCostPartitioningAlgorithm::get_cost_partitioned_heuristic_value(
    const LandmarkStatusManager &lm_status_manager,
    const State &ancestor_state) {
    vector<int> achieved_lms_by_op(operator_costs.size(), 0);
    vector<bool> action_landmarks(operator_costs.size(), false);

    const LandmarkGraph::Nodes &nodes = lm_graph.get_nodes();
    ConstBitsetView past =
        lm_status_manager.get_past_landmarks(ancestor_state);
    ConstBitsetView future =
        lm_status_manager.get_future_landmarks(ancestor_state);

    double h = 0;

    /* First pass:
       compute which op achieves how many landmarks. Along the way,
       mark action landmarks and add their cost to h. */
    for (auto &node : nodes) {
        int id = node->get_id();
        if (future.test(id)) {
            const Achievers &achievers =
                get_achievers(node->get_landmark(), past.test(id));
            if (achievers.empty())
                return numeric_limits<double>::max();
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

    /* TODO: Replace with Landmarks (to do so, we need some way to access the
        status of a Landmark without access to the ID, which is part of
        LandmarkNode). */
    vector<const LandmarkNode *> relevant_lms;

    /* Second pass:
       remove landmarks from consideration that are covered by
       an action landmark; decrease the counters accordingly
       so that no unnecessary cost is assigned to these landmarks. */
    for (auto &node : nodes) {
        int id = node->get_id();
        if (future.test(id)) {
            const Achievers &achievers =
                get_achievers(node->get_landmark(), past.test(id));
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
                relevant_lms.push_back(node.get());
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
            // TODO: Iterate over Landmarks instead of LandmarkNodes
            int id = node->get_id();
            assert(future.test(id));
            const Achievers &achievers = get_achievers(node->get_landmark(), past.test(id));
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
            int id = node->get_id();
            assert(future.test(id));
            const Achievers &achievers = get_achievers(node->get_landmark(), past.test(id));
            double min_cost = numeric_limits<double>::max();
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, achieved_lms_by_op));
                int num_achieved = achieved_lms_by_op[op_id];
                assert(num_achieved >= 1);
                assert(utils::in_bounds(op_id, operator_costs));
                double partitioned_cost = static_cast<double>(operator_costs[op_id]) / num_achieved;
                min_cost = min(min_cost, partitioned_cost);
            }
            h += min_cost;
        }
    }

    return h;
}


LandmarkCanonicalHeuristic::LandmarkCanonicalHeuristic(
    const vector<int> &operator_costs,
    const LandmarkGraph &graph)
    : CostPartitioningAlgorithm(operator_costs, graph) {
}

static bool empty_intersection(const Achievers &x, const Achievers &y) {
    for (int a : x) {
        if (y.find(a) != y.end()) {
            return false;
        }
    }
    return true;
}

vector<vector<int>> LandmarkCanonicalHeuristic::compute_max_additive_subsets(
    const ConstBitsetView &past_landmarks,
    const vector<const LandmarkNode *> &relevant_landmarks) {
    int num_landmarks = relevant_landmarks.size();

    // Initialize compatibility graph.
    vector<vector<int>> cgraph;
    cgraph.resize(num_landmarks);

    for (int i = 0; i < num_landmarks; ++i) {
        const LandmarkNode *lm1 = relevant_landmarks[i];
        int id1 = lm1->get_id();
        const Achievers &achievers1 = get_achievers(lm1->get_landmark(), past_landmarks.test(id1));
        for (int j = i + 1; j < num_landmarks; ++j) {
            const LandmarkNode *lm2 = relevant_landmarks[j];
            int id2 = lm2->get_id();
            const Achievers &achievers2 = get_achievers(lm2->get_landmark(), past_landmarks.test(id2));
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

int LandmarkCanonicalHeuristic::compute_minimum_landmark_cost(
    const LandmarkNode &lm_node, bool past) const {
    const Achievers &achievers = get_achievers(lm_node.get_landmark(), past);
    assert(!achievers.empty());
    int min_cost = numeric_limits<int>::max();
    for (int op_id : achievers) {
        assert(utils::in_bounds(op_id, operator_costs));
        min_cost = min(min_cost, operator_costs[op_id]);
    }
    return min_cost;
}

double LandmarkCanonicalHeuristic::get_cost_partitioned_heuristic_value(
    const LandmarkStatusManager &lm_status_manager,
    const State &ancestor_state) {
    ConstBitsetView past =
        lm_status_manager.get_past_landmarks(ancestor_state);
    ConstBitsetView future =
        lm_status_manager.get_future_landmarks(ancestor_state);

    // Ignore reached landmarks.
    vector<const LandmarkNode *> relevant_landmarks;
    for (auto &node : lm_graph.get_nodes()) {
        if (future.test(node->get_id())) {
            relevant_landmarks.push_back(node.get());
        }
    }

    vector<vector<int>> max_additive_subsets = compute_max_additive_subsets(
        past, relevant_landmarks);

    vector<int> minimum_landmark_costs;
    minimum_landmark_costs.reserve(relevant_landmarks.size());
    for (const LandmarkNode *node : relevant_landmarks) {
        minimum_landmark_costs.push_back(compute_minimum_landmark_cost(*node, past.test(node->get_id())));
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
    : CostPartitioningAlgorithm(operator_costs, graph),
      lp_solver(solver_type),
      lp(build_initial_lp()) {
}

lp::LinearProgram LandmarkPhO::build_initial_lp() {
    /* The LP has one variable (column) per landmark and one
       inequality (row) per operator. */
    int num_cols = lm_graph.get_num_landmarks();
    int num_rows = operator_costs.size();

    named_vector::NamedVector<lp::LPVariable> lp_variables;

    /* We want to maximize \sum_i w_i * cost(lm_i) * [lm_i not achieved],
       where cost(lm_i) is the cost of the cheapest operator achieving lm_i.
       We adapt the variable bounds in each state to ignore achieved landmarks
       and initialize the range to [0.0, 0.0]. */
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode *lm = lm_graph.get_node(lm_id);
        double min_cost = compute_landmark_cost(*lm, false);
        lp_variables.emplace_back(0.0, 0.0, min_cost);
    }

    /*
      Set the constraint bounds. The constraints for operator o are of the form
      w_1 + w_5 + ... + w_k <= 1
      where w_1, w_5, ..., w_k are the weights for the landmarks for which o is
      a relevant achiever.
    */
    lp_constraints.resize(num_rows, lp::LPConstraint(-lp_solver.get_infinity(), 1.0));

    /* Coefficients of constraints will be updated and recreated in each state.
       We ignore them for the initial LP. */
    return lp::LinearProgram(
        lp::LPObjectiveSense::MAXIMIZE,
        move(lp_variables),
        {},
        lp_solver.get_infinity());
}

double LandmarkPhO::compute_landmark_cost(const LandmarkNode &lm, bool past) const {
    /* Note that there are landmarks without achievers. Example: not-served(p)
       in miconic:s1-0.pddl. The fact is true in the initial state, and no
       operator achieves it. For such facts, the (infimum) cost is infinity. */
    const Achievers &achievers = get_achievers(lm.get_landmark(), past);
    double min_cost = lp_solver.get_infinity();
    for (int op_id : achievers) {
        assert(utils::in_bounds(op_id, operator_costs));
        min_cost = min(min_cost, static_cast<double>(operator_costs[op_id]));
    }
    return min_cost;
}

double LandmarkPhO::get_cost_partitioned_heuristic_value(
    const LandmarkStatusManager &lm_status_manager,
    const State &ancestor_state) {
    const ConstBitsetView past = lm_status_manager.get_past_landmarks(ancestor_state);
    const ConstBitsetView future = lm_status_manager.get_future_landmarks(ancestor_state);
    /*
      Set up LP variable bounds for the landmarks.
      The range of w_i is {0} if the corresponding landmark is already
      reached; otherwise it is [0, infinity].
      The lower bounds are set to 0 initially and never change.
    */
    int num_cols = lm_graph.get_num_landmarks();
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        double upper_bound = future.test(lm_id) ? lp_solver.get_infinity() : 0.0;
        lp.get_variables()[lm_id].upper_bound = upper_bound;
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
        const LandmarkNode *lm = lm_graph.get_node(lm_id);
        if (future.test(lm_id)) {
            const Achievers &achievers = get_achievers(lm->get_landmark(), past.test(lm_id));
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
    lp.get_constraints().clear();
    for (const lp::LPConstraint &constraint : lp_constraints) {
        if (!constraint.empty())
            lp.get_constraints().push_back(constraint);
    }

    // Load the problem into the LP solver.
    lp_solver.load_problem(lp);

    // Solve the linear program.
    lp_solver.solve();

    assert(lp_solver.has_optimal_solution());
    double h = lp_solver.get_objective_value();

    return h;
}


OptimalCostPartitioningAlgorithm::OptimalCostPartitioningAlgorithm(
    const vector<int> &operator_costs, const LandmarkGraph &graph,
    lp::LPSolverType solver_type)
    : CostPartitioningAlgorithm(operator_costs, graph),
      lp_solver(solver_type),
      lp(build_initial_lp()) {
}

lp::LinearProgram OptimalCostPartitioningAlgorithm::build_initial_lp() {
    /* The LP has one variable (column) per landmark and one
       inequality (row) per operator. */
    int num_cols = lm_graph.get_num_landmarks();
    int num_rows = operator_costs.size();

    named_vector::NamedVector<lp::LPVariable> lp_variables;

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

    /* Coefficients of constraints will be updated and recreated in each state.
       We ignore them for the initial LP. */
    return lp::LinearProgram(lp::LPObjectiveSense::MAXIMIZE, move(lp_variables),
                             {}, lp_solver.get_infinity());
}

double OptimalCostPartitioningAlgorithm::get_cost_partitioned_heuristic_value(
    const LandmarkStatusManager &lm_status_manager,
    const State &ancestor_state) {
    /* TODO: We could also do the same thing with action landmarks we
             do in the uniform cost partitioning case. */


    ConstBitsetView past =
        lm_status_manager.get_past_landmarks(ancestor_state);
    ConstBitsetView future =
        lm_status_manager.get_future_landmarks(ancestor_state);
    /*
      Set up LP variable bounds for the landmarks.
      The range of cost(lm_1) is {0} if the landmark is already
      reached; otherwise it is [0, infinity].
      The lower bounds are set to 0 in the constructor and never change.
    */
    int num_cols = lm_graph.get_num_landmarks();
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        if (future.test(lm_id)) {
            lp.get_variables()[lm_id].upper_bound = lp_solver.get_infinity();
        } else {
            lp.get_variables()[lm_id].upper_bound = 0;
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
        const Landmark &landmark = lm_graph.get_node(lm_id)->get_landmark();
        if (future.test(lm_id)) {
            const Achievers &achievers =
                get_achievers(landmark, past.test(lm_id));
            if (achievers.empty())
                return numeric_limits<double>::max();
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, lp_constraints));
                lp_constraints[op_id].insert(lm_id, 1.0);
            }
        }
    }

    /* Copy non-empty constraints and use those in the LP.
       This significantly speeds up the heuristic calculation. See issue443. */
    // TODO: do not copy the data here.
    lp.get_constraints().clear();
    for (const lp::LPConstraint &constraint : lp_constraints) {
        if (!constraint.empty())
            lp.get_constraints().push_back(constraint);
    }

    // Load the problem into the LP solver.
    lp_solver.load_problem(lp);

    // Solve the linear program.
    lp_solver.solve();

    assert(lp_solver.has_optimal_solution());
    double h = lp_solver.get_objective_value();

    return h;
}
}

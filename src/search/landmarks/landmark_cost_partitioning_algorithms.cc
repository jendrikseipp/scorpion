#include "landmark_cost_partitioning_algorithms.h"

#include "landmark.h"
#include "landmark_graph.h"
#include "landmark_status_manager.h"

#include "../algorithms/max_cliques.h"
#include "../cost_saturation/greedy_order_utils.h"
#include "../cost_saturation/types.h"
#include "../utils/collections.h"
#include "../utils/language.h"
#include "../utils/logging.h"
#include "../utils/rng.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <numeric>

using namespace std;
using cost_saturation::ScoringFunction;

namespace landmarks {
CostPartitioningAlgorithm::CostPartitioningAlgorithm(
    const vector<int> &operator_costs, const LandmarkGraph &graph)
    : landmark_graph(graph), operator_costs(operator_costs) {
}

static const unordered_set<int> &get_achievers(
    const Landmark &landmark, const bool past) {
    // Return relevant achievers of the landmark according to its status.
    return past ? landmark.possible_achievers : landmark.first_achievers;
}

UniformCostPartitioningAlgorithm::UniformCostPartitioningAlgorithm(
    const vector<int> &operator_costs, const LandmarkGraph &graph,
    bool use_action_landmarks, bool reuse_costs, bool greedy,
    enum cost_saturation::ScoringFunction scoring_function,
    const shared_ptr<utils::RandomNumberGenerator> &rng)
    : CostPartitioningAlgorithm(operator_costs, graph),
      use_action_landmarks(use_action_landmarks),
      reuse_costs(reuse_costs),
      greedy(greedy),
      scoring_function(scoring_function),
      rng(rng),
      original_costs(operator_costs.begin(), operator_costs.end()) {
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

/* Compute which operator achieves how many landmarks. Along the way, mark
   action landmarks and sum up their costs. */
double UniformCostPartitioningAlgorithm::first_pass(
    vector<int> &landmarks_achieved_by_operator, vector<bool> &action_landmarks,
    ConstBitsetView &past, ConstBitsetView &future) {
    double action_landmarks_cost = 0;
    for (const auto &node : landmark_graph) {
        int id = node->get_id();
        if (future.test(id)) {
            const unordered_set<int> &achievers =
                get_achievers(node->get_landmark(), past.test(id));
            if (achievers.empty()) {
                return numeric_limits<double>::max();
            }
            if (use_action_landmarks && achievers.size() == 1) {
                // We have found an action landmark for this state.
                int op_id = *achievers.begin();
                if (!action_landmarks[op_id]) {
                    action_landmarks[op_id] = true;
                    assert(utils::in_bounds(op_id, operator_costs));
                    action_landmarks_cost += operator_costs[op_id];
                }
            } else {
                for (int op_id : achievers) {
                    assert(utils::in_bounds(
                        op_id, landmarks_achieved_by_operator));
                    ++landmarks_achieved_by_operator[op_id];
                }
            }
        }
    }
    return action_landmarks_cost;
}

/*
  Collect all landmarks that are not covered by action landmarks. For all
  landmarks that are covered, reduce the number of landmarks achieved by their
  achievers to strengthen the cost partitioning.
*/
vector<const LandmarkNode *> UniformCostPartitioningAlgorithm::second_pass(
    vector<int> &landmarks_achieved_by_operator,
    const vector<bool> &action_landmarks, ConstBitsetView &past,
    ConstBitsetView &future) {
    vector<const LandmarkNode *> uncovered_landmarks;
    for (const auto &node : landmark_graph) {
        int id = node->get_id();
        if (future.test(id)) {
            const unordered_set<int> &achievers =
                get_achievers(node->get_landmark(), past.test(id));
            bool covered_by_action_landmark = false;
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, action_landmarks));
                if (action_landmarks[op_id]) {
                    covered_by_action_landmark = true;
                    break;
                }
            }
            if (covered_by_action_landmark) {
                for (int op_id : achievers) {
                    assert(utils::in_bounds(
                        op_id, landmarks_achieved_by_operator));
                    --landmarks_achieved_by_operator[op_id];
                }
            } else {
                uncovered_landmarks.push_back(node.get());
            }
        }
    }
    return uncovered_landmarks;
}

// Compute the cost partitioning.
double UniformCostPartitioningAlgorithm::third_pass(
    const vector<const LandmarkNode *> &uncovered_landmarks,
    vector<int> &landmarks_achieved_by_operator, ConstBitsetView &past,
    ConstBitsetView &future) {
    double cost = 0;

    if (reuse_costs || greedy) {
        // UOCP + ZOCP + SCP
        remaining_costs = original_costs;
        vector<vector<int>> achievers_by_lm;
        achievers_by_lm.reserve(uncovered_landmarks.size());
        for (const LandmarkNode *node : uncovered_landmarks) {
            // TODO: Iterate over Landmarks instead of LandmarkNodes.
            int id = node->get_id();
            assert(future.test(id));
            const unordered_set<int> &achievers =
                get_achievers(node->get_landmark(), past.test(id));
            achievers_by_lm.emplace_back(achievers.begin(), achievers.end());
        }
        for (int lm_id : compute_landmark_order(achievers_by_lm)) {
            const vector<int> &achievers = achievers_by_lm[lm_id];
            double min_cost = numeric_limits<double>::max();
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, landmarks_achieved_by_operator));
                int num_achieved = landmarks_achieved_by_operator[op_id];
                assert(num_achieved >= 1);
                assert(utils::in_bounds(op_id, remaining_costs));
                double partitioned_cost =
                    greedy ? remaining_costs[op_id]
                           : remaining_costs[op_id] / num_achieved;
                min_cost = min(min_cost, partitioned_cost);
            }
            cost += min_cost;
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
                --landmarks_achieved_by_operator[op_id];
            }
        }
    } else {
        // UCP
        for (const LandmarkNode *node : uncovered_landmarks) {
            // TODO: Iterate over Landmarks instead of LandmarkNodes.
            int id = node->get_id();
            assert(future.test(id));
            utils::unused_variable(future);
            const unordered_set<int> &achievers =
                get_achievers(node->get_landmark(), past.test(id));
            double min_cost = numeric_limits<double>::max();
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, landmarks_achieved_by_operator));
                int num_achieved = landmarks_achieved_by_operator[op_id];
                assert(num_achieved >= 1);
                assert(utils::in_bounds(op_id, operator_costs));
                double partitioned_cost =
                    static_cast<double>(operator_costs[op_id]) / num_achieved;
                min_cost = min(min_cost, partitioned_cost);
            }
            cost += min_cost;
        }
    }
    return cost;
}

double UniformCostPartitioningAlgorithm::get_cost_partitioned_heuristic_value(
    const LandmarkStatusManager &landmark_status_manager,
    const State &ancestor_state) {
    vector<int> landmarks_achieved_by_operator(operator_costs.size(), 0);
    vector<bool> action_landmarks(operator_costs.size(), false);

    ConstBitsetView past =
        landmark_status_manager.get_past_landmarks(ancestor_state);
    ConstBitsetView future =
        landmark_status_manager.get_future_landmarks(ancestor_state);

    const double cost_of_action_landmarks = first_pass(
        landmarks_achieved_by_operator, action_landmarks, past, future);
    if (cost_of_action_landmarks == numeric_limits<double>::max()) {
        return cost_of_action_landmarks;
    }

    /*
      TODO: Use landmarks instead of landmark nodes. To do so, we need
       some way to access the status of a Landmark without access to the
       ID which is part of landmark node.
    */
    const vector<const LandmarkNode *> uncovered_landmarks = second_pass(
        landmarks_achieved_by_operator, action_landmarks, past, future);

    const double cost_partitioning_cost = third_pass(
        uncovered_landmarks, landmarks_achieved_by_operator, past, future);

    return cost_of_action_landmarks + cost_partitioning_cost;
}

LandmarkCanonicalHeuristic::LandmarkCanonicalHeuristic(
    const vector<int> &operator_costs, const LandmarkGraph &graph)
    : CostPartitioningAlgorithm(operator_costs, graph) {
}

static bool empty_intersection(
    const unordered_set<int> &x, const unordered_set<int> &y) {
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
        const unordered_set<int> &achievers1 =
            get_achievers(lm1->get_landmark(), past_landmarks.test(id1));
        for (int j = i + 1; j < num_landmarks; ++j) {
            const LandmarkNode *lm2 = relevant_landmarks[j];
            int id2 = lm2->get_id();
            const unordered_set<int> &achievers2 =
                get_achievers(lm2->get_landmark(), past_landmarks.test(id2));
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
    const unordered_set<int> &achievers =
        get_achievers(lm_node.get_landmark(), past);
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
    ConstBitsetView past = lm_status_manager.get_past_landmarks(ancestor_state);
    ConstBitsetView future =
        lm_status_manager.get_future_landmarks(ancestor_state);

    // Ignore reached landmarks.
    vector<const LandmarkNode *> relevant_landmarks;
    for (auto &node : landmark_graph) {
        if (future.test(node->get_id())) {
            relevant_landmarks.push_back(node.get());
        }
    }

    vector<vector<int>> max_additive_subsets =
        compute_max_additive_subsets(past, relevant_landmarks);

    vector<int> minimum_landmark_costs;
    minimum_landmark_costs.reserve(relevant_landmarks.size());
    for (const LandmarkNode *node : relevant_landmarks) {
        minimum_landmark_costs.push_back(
            compute_minimum_landmark_cost(*node, past.test(node->get_id())));
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
    const vector<int> &operator_costs, const LandmarkGraph &graph,
    bool saturate, lp::LPSolverType solver_type)
    : CostPartitioningAlgorithm(operator_costs, graph),
      saturate(saturate),
      lp_solver(solver_type),
      lp(build_initial_lp()) {
}

lp::LinearProgram LandmarkPhO::build_initial_lp() {
    /* The LP has one variable (column) per landmark and one
       inequality (row) per operator. */
    int num_cols = landmark_graph.get_num_landmarks();
    int num_rows = operator_costs.size();

    // We adapt the variable coefficient and bounds for each state below.
    named_vector::NamedVector<lp::LPVariable> lp_variables;
    lp_variables.resize(num_cols, {0.0, 0.0, 1.0});

    /*
      Set the constraint bounds. The constraints for operator o are of the form
      w_1 + w_5 + ... + w_k <= 1
      where w_1, w_5, ..., w_k are the weights for the landmarks for which o is
      a relevant achiever.
    */
    lp_constraints.resize(
        num_rows, lp::LPConstraint(-lp_solver.get_infinity(), 1.0));
    if (saturate) {
        for (int i = 0; i < num_rows; ++i) {
            lp_constraints[i].set_upper_bound(operator_costs[i]);
        }
    }

    /* Coefficients of constraints will be updated and recreated in each state.
       We ignore them for the initial LP. */
    return lp::LinearProgram(
        lp::LPObjectiveSense::MAXIMIZE, move(lp_variables), {},
        lp_solver.get_infinity());
}

double LandmarkPhO::compute_landmark_cost(
    const LandmarkNode &lm, bool past) const {
    /* Note that there are landmarks without achievers. Example: not-served(p)
       in miconic:s1-0.pddl. The fact is true in the initial state, and no
       operator achieves it. For such facts, the (infimum) cost is infinity. */
    const unordered_set<int> &achievers =
        get_achievers(lm.get_landmark(), past);
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
    const ConstBitsetView past =
        lm_status_manager.get_past_landmarks(ancestor_state);
    const ConstBitsetView future =
        lm_status_manager.get_future_landmarks(ancestor_state);
    /*
      We want to maximize \sum_i w_i * cost(lm_i) * [lm_i not achieved],
      where cost(lm_i) is the cost of the cheapest operator achieving lm_i.
      Note that the set of achievers depends on whether the landmark has been
      achieved before. The upper bound for w_i is infinity if the corresponding
      landmark still has to be reached (again); otherwise it is 0. The lower
      bounds are set to 0 initially and never change.
    */
    int num_cols = landmark_graph.get_num_landmarks();
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode &lm_node = *landmark_graph.get_node(lm_id);
        double lm_cost = compute_landmark_cost(lm_node, past.test(lm_id));
        double upper_bound =
            future.test(lm_id) ? lp_solver.get_infinity() : 0.0;
        auto &lm_var = lp.get_variables()[lm_id];
        lm_var.objective_coefficient = lm_cost;
        lm_var.upper_bound = upper_bound;
    }

    /*
      Define the constraint matrix. The constraints for operator o are of the
      form w_1 + w_5 + ... + w_k <= 1 where w_1, w_5, ..., w_k are the weights
      for the landmarks for which o is a relevant achiever. Hence, we add a
      triple (op, lm, 1.0) for each relevant achiever op of landmark lm,
      denoting that in the op-th row and lm-th column, the matrix has a 1.0
      entry.
    */
    // Reuse previous constraint objects to save the effort of recreating them.
    for (lp::LPConstraint &constraint : lp_constraints) {
        constraint.clear();
    }
    for (int lm_id = 0; lm_id < num_cols; ++lm_id) {
        const LandmarkNode &lm = *landmark_graph.get_node(lm_id);
        if (future.test(lm_id)) {
            const unordered_set<int> &achievers =
                get_achievers(lm.get_landmark(), past.test(lm_id));
            if (achievers.empty()) {
                return numeric_limits<double>::max();
            }
            // The saturated costs are equal to the cost of the landmark.
            double coeff = saturate
                               ? lp.get_variables()[lm_id].objective_coefficient
                               : 1.0;
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, lp_constraints));
                lp_constraints[op_id].insert(lm_id, coeff);
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
    return lp_solver.get_objective_value();
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
    const int num_cols = landmark_graph.get_num_landmarks();
    const int num_rows = operator_costs.size();

    named_vector::NamedVector<lp::LPVariable> lp_variables;

    /*
      We want to maximize 1 * cost(lm_1) + ... + 1 * cost(lm_n), so the
      coefficients are all 1.
      Variable bounds are state-dependent; we initialize the range to {0}.
    */
    lp_variables.resize(num_cols, lp::LPVariable(0.0, 0.0, 1.0));

    /*
      Set up lower bounds and upper bounds for the inequalities. These simply
      say that the operator's total cost must fall between 0 and the real
      operator cost.
    */
    lp_constraints.resize(num_rows, lp::LPConstraint(0.0, 0.0));
    for (size_t op_id = 0; op_id < operator_costs.size(); ++op_id) {
        lp_constraints[op_id].set_lower_bound(0);
        lp_constraints[op_id].set_upper_bound(operator_costs[op_id]);
    }

    /* Coefficients of constraints will be updated and recreated in each state.
       We ignore them for the initial LP. */
    return lp::LinearProgram(
        lp::LPObjectiveSense::MAXIMIZE, move(lp_variables), {},
        lp_solver.get_infinity());
}

/*
  Set up LP variable bounds for the landmarks. The range of cost(lm_1) is {0} if
  the landmark is already reached; otherwise it is [0, infinity]. The lower
  bounds are set to 0 in the constructor and never change.
*/
void OptimalCostPartitioningAlgorithm::set_lp_bounds(
    ConstBitsetView &future, const int num_cols) {
    for (int id = 0; id < num_cols; ++id) {
        if (future.test(id)) {
            lp.get_variables()[id].upper_bound = lp_solver.get_infinity();
        } else {
            lp.get_variables()[id].upper_bound = 0;
        }
    }
}

/*
  Define the constraint matrix. The constraints are of the form
  cost(lm_i1) + cost(lm_i2) + ... + cost(lm_in) <= cost(o)
  where lm_i1 ... lm_in are the landmarks for which o is a relevant achiever.
  Hence, we add a triple (op, lm, 1.0) for each relevant achiever op of
  landmark lm, denoting that in the op-th row and lm-th column, the matrix has
  a 1.0 entry.
  Returns true if the current state is a dead-end.
*/
bool OptimalCostPartitioningAlgorithm::define_constraint_matrix(
    ConstBitsetView &past, ConstBitsetView &future, const int num_cols) {
    // Reuse previous constraint objects to save the effort of recreating them.
    for (lp::LPConstraint &constraint : lp_constraints) {
        constraint.clear();
    }
    for (int id = 0; id < num_cols; ++id) {
        const Landmark &landmark = landmark_graph.get_node(id)->get_landmark();
        if (future.test(id)) {
            const unordered_set<int> &achievers =
                get_achievers(landmark, past.test(id));
            /*
              TODO: We could deal with things more uniformly by just adding a
               constraint with no variables because there are no achievers
               (instead of returning here), which would then be detected as an
               unsolvable constraint by the LP solver. However, as of now this
               does not work because `get_cost_partitioned_heuristic_value` only
               adds non-empty constraints to the LP. We should implement this
               differently, which requires a solution that does not reuse
               constraints from the previous iteration as it does now.
            */
            if (achievers.empty()) {
                return true;
            }
            for (int op_id : achievers) {
                assert(utils::in_bounds(op_id, lp_constraints));
                lp_constraints[op_id].insert(id, 1.0);
            }
        }
    }
    return false;
}

double OptimalCostPartitioningAlgorithm::get_cost_partitioned_heuristic_value(
    const LandmarkStatusManager &landmark_status_manager,
    const State &ancestor_state) {
    /* TODO: We could also do the same thing with action landmarks we do in the
        uniform cost partitioning case. */

    ConstBitsetView past =
        landmark_status_manager.get_past_landmarks(ancestor_state);
    ConstBitsetView future =
        landmark_status_manager.get_future_landmarks(ancestor_state);

    const int num_cols = landmark_graph.get_num_landmarks();
    set_lp_bounds(future, num_cols);
    const bool dead_end = define_constraint_matrix(past, future, num_cols);
    if (dead_end) {
        return numeric_limits<double>::max();
    }

    /* Copy non-empty constraints and use those in the LP.
       This significantly speeds up the heuristic calculation. See issue443. */
    // TODO: Do not copy the data here.
    lp.get_constraints().clear();
    for (const lp::LPConstraint &constraint : lp_constraints) {
        if (!constraint.empty()) {
            lp.get_constraints().push_back(constraint);
        }
    }

    lp_solver.load_problem(lp);
    lp_solver.solve();

    assert(lp_solver.has_optimal_solution());
    return lp_solver.get_objective_value();
}
}

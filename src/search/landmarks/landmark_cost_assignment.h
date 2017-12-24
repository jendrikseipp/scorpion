#ifndef LANDMARKS_LANDMARK_COST_ASSIGNMENT_H
#define LANDMARKS_LANDMARK_COST_ASSIGNMENT_H

#include "../lp/lp_solver.h"

#include <set>
#include <vector>

class OperatorsProxy;

namespace cost_saturation {
enum class ScoringFunction;
}

namespace utils {
class RandomNumberGenerator;
}

namespace landmarks {
class LandmarkGraph;
class LandmarkNode;

class LandmarkCostAssignment {
    const std::set<int> empty;
protected:
    const LandmarkGraph &lm_graph;
    const std::vector<int> operator_costs;

    const std::set<int> &get_achievers(int lmn_status,
                                       const LandmarkNode &lmn) const;
public:
    LandmarkCostAssignment(const std::vector<int> &operator_costs,
                           const LandmarkGraph &graph);
    virtual ~LandmarkCostAssignment() = default;

    virtual double cost_sharing_h_value() = 0;
};

class LandmarkUniformSharedCostAssignment : public LandmarkCostAssignment {
    const bool use_action_landmarks;
    const bool reuse_costs;
    const bool greedy;
    const cost_saturation::ScoringFunction scoring_function;

    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    // Store vectors as members to avoid allocations.
    const std::vector<double> original_costs;
    std::vector<double> remaining_costs;

    std::vector<int> compute_landmark_order(
        const std::vector<std::vector<int>> &achievers_by_lm);

public:
    LandmarkUniformSharedCostAssignment(const std::vector<int> &operator_costs,
                                        const LandmarkGraph &graph,
                                        bool use_action_landmarks,
                                        bool reuse_costs,
                                        bool greedy,
                                        enum cost_saturation::ScoringFunction,
                                        const std::shared_ptr<utils::RandomNumberGenerator> &rng);

    virtual double cost_sharing_h_value() override;
};

class LandmarkCanonicalHeuristic : public LandmarkCostAssignment {
    std::vector<std::vector<int>> compute_max_additive_subsets(
        const std::vector<const LandmarkNode *> &relevant_landmarks);
    int compute_minimum_landmark_cost(const LandmarkNode &lm) const;
public:
    LandmarkCanonicalHeuristic(
        const std::vector<int> &operator_costs,
        const LandmarkGraph &graph);

    virtual double cost_sharing_h_value() override;
};

class LandmarkPhO : public LandmarkCostAssignment {
    // See comment for LandmarkEfficientOptimalSharedCostAssignment.
    lp::LPSolver lp_solver;
    std::vector<lp::LPVariable> lp_variables;
    std::vector<lp::LPConstraint> lp_constraints;
    std::vector<lp::LPConstraint> non_empty_lp_constraints;

    int compute_minimum_landmark_cost(const LandmarkNode &lm) const;
public:
    LandmarkPhO(
        const std::vector<int> &operator_costs,
        const LandmarkGraph &graph,
        lp::LPSolverType solver_type);

    virtual double cost_sharing_h_value() override;
};

class LandmarkEfficientOptimalSharedCostAssignment : public LandmarkCostAssignment {
    lp::LPSolver lp_solver;
    /*
      We keep the vectors for LP variables and constraints around instead of
      recreating them for every state. The actual constraints have to be
      recreated because the coefficient matrix of the LP changes from state to
      state. Reusing the vectors still saves some dynamic allocation overhead.
     */
    std::vector<lp::LPVariable> lp_variables;
    std::vector<lp::LPConstraint> lp_constraints;
    std::vector<lp::LPConstraint> non_empty_lp_constraints;
public:
    LandmarkEfficientOptimalSharedCostAssignment(const std::vector<int> &operator_costs,
                                                 const LandmarkGraph &graph,
                                                 lp::LPSolverType solver_type);

    virtual double cost_sharing_h_value() override;
};
}

#endif

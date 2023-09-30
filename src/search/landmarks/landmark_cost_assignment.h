#ifndef LANDMARKS_LANDMARK_COST_ASSIGNMENT_H
#define LANDMARKS_LANDMARK_COST_ASSIGNMENT_H

#include "landmark.h"

#include "../lp/lp_solver.h"

#include <vector>

class OperatorsProxy;

namespace cost_saturation {
enum class ScoringFunction;
}

namespace utils {
class RandomNumberGenerator;
}

namespace landmarks {
class Landmark;
class LandmarkGraph;
class LandmarkNode;
class LandmarkStatusManager;

class LandmarkCostAssignment {
    const Landmark::Achievers empty;
protected:
    const LandmarkGraph &lm_graph;
    const std::vector<int> operator_costs;

    const Landmark::Achievers &get_achievers(
        int lmn_status, const Landmark &landmark) const;
public:
    LandmarkCostAssignment(const std::vector<int> &operator_costs,
                           const LandmarkGraph &graph);
    virtual ~LandmarkCostAssignment() = default;

    virtual double cost_sharing_h_value(
        const LandmarkStatusManager &lm_status_manager) = 0;
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

    virtual double cost_sharing_h_value(
        const LandmarkStatusManager &lm_status_manager) override;
};

class LandmarkCanonicalHeuristic : public LandmarkCostAssignment {
    std::vector<std::vector<int>> compute_max_additive_subsets(
        const LandmarkStatusManager &lm_status_manager,
        const std::vector<const LandmarkNode *> &relevant_landmarks);
    int compute_minimum_landmark_cost(const LandmarkNode &lm, int lm_status) const;
public:
    LandmarkCanonicalHeuristic(
        const std::vector<int> &operator_costs,
        const LandmarkGraph &graph);

    virtual double cost_sharing_h_value(
        const LandmarkStatusManager &lm_status_manager) override;
};

class LandmarkPhO : public LandmarkCostAssignment {
    // See comment for LandmarkEfficientOptimalSharedCostAssignment.
    lp::LPSolver lp_solver;
    std::vector<lp::LPConstraint> lp_constraints;
    lp::LinearProgram lp;

    lp::LinearProgram build_initial_lp();

    double compute_landmark_cost(const LandmarkNode &lm) const;
public:
    LandmarkPhO(
        const std::vector<int> &operator_costs,
        const LandmarkGraph &graph,
        lp::LPSolverType solver_type);

    virtual double cost_sharing_h_value(
        const LandmarkStatusManager &lm_status_manager) override;
};

class LandmarkEfficientOptimalSharedCostAssignment : public LandmarkCostAssignment {
    lp::LPSolver lp_solver;
    // We keep an additional copy of the constraints around to avoid some effort with recreating the vector (see issue443).
    std::vector<lp::LPConstraint> lp_constraints;
    /*
      We keep the vectors for LP variables and constraints around instead of
      recreating them for every state. The actual constraints have to be
      recreated because the coefficient matrix of the LP changes from state to
      state. Reusing the vectors still saves some dynamic allocation overhead.
     */
    lp::LinearProgram lp;

    lp::LinearProgram build_initial_lp();
public:
    LandmarkEfficientOptimalSharedCostAssignment(
        const std::vector<int> &operator_costs,
        const LandmarkGraph &graph,
        lp::LPSolverType solver_type);

    virtual double cost_sharing_h_value(
        const LandmarkStatusManager &lm_status_manager) override;
};
}

#endif

#include "ocp_constraints.h"

#include "../lp/lp_solver.h"

#include <cassert>
#include <limits>
#include <memory>
#include <vector>

using namespace std;

namespace cegar {
OCPConstraints::OCPConstraints(const Abstraction &abstraction) {
    utils::unused_variable(abstraction);
}

void OCPConstraints::initialize_constraints(
    const std::shared_ptr<AbstractTask> task,
    vector<lp::LPConstraint> &constraints,
    double infinity) {
    utils::unused_variable(task);
    utils::unused_variable(constraints);
    utils::unused_variable(infinity);
}

bool OCPConstraints::update_constraints(const State &state,
                                        lp::LPSolver &lp_solver) {
    utils::unused_variable(state);
    utils::unused_variable(lp_solver);
    return false;
}
}

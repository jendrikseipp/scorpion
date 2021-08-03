#include "flaw_selector_fast.h"

#include "abstract_state.h"
#include "abstraction.h"
#include "split_selector.h"
#include "transition.h"
#include "transition_system.h"
#include "types.h"
#include "utils.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

#include <cassert>
#include <queue>
#include <stack>

using namespace std;

namespace cegar {
unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_optimistic(
    const Abstraction &abstraction,
    const vector<int> &domain_sizes,
    const Solution &solution,
    utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_backtrack_pessimistic(
    const Abstraction &abstraction,
    const vector<int> &domain_sizes,
    const Solution &solution,
    utils::RandomNumberGenerator &rng) const {
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_optimistic(const Abstraction &abstraction,
                                   const vector<int> &domain_sizes,
                                   const Solution &solution,
                                   utils::RandomNumberGenerator &rng) const {
    const AbstractState *abstract_state = &abstraction.get_initial_state();

    for (size_t step_id = 0; step_id < solution.size(); ++step_id) {
        if (!utils::extra_memory_padding_is_reserved())
            break;
    }
    return nullptr;
}

unique_ptr<Flaw>
FlawSelector::find_flaw_pessimistic(const Abstraction &abstraction,
                                    const vector<int> &domain_sizes,
                                    const Solution &solution,
                                    utils::RandomNumberGenerator &rng) const {
    return nullptr;
}
}

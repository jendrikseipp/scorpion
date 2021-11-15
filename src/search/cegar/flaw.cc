#include "flaw.h"

#include "abstraction.h"
#include "abstract_state.h"
#include "split_selector.h"
#include "transition.h"


using namespace std;

namespace cegar {
Flaw::Flaw(State &&concrete_state,
           CartesianSet &&desired_cartesian_set,
           int abstract_state_id,
           int h_value)
    : concrete_state(move(concrete_state)),
      desired_cartesian_set(move(desired_cartesian_set)),
      abstract_state_id(abstract_state_id),
      h_value(h_value) {
}

vector<Split> Flaw::get_possible_splits(const Abstraction &abstraction) const {
    vector<Split> splits;
    /*
      For each fact in the concrete state that is not contained in the
      desired abstract state, loop over all values in the domain of the
      corresponding variable. The values that are in both the current and
      the desired abstract state are the "wanted" ones, i.e., the ones that
      we want to split off.
    */
    for (FactProxy wanted_fact_proxy : concrete_state) {
        const AbstractState &abstract_state =
            abstraction.get_state(abstract_state_id);
        FactPair fact = wanted_fact_proxy.get_pair();
        if (!desired_cartesian_set.test(fact.var, fact.value)) {
            VariableProxy var = wanted_fact_proxy.get_variable();
            int var_id = var.get_id();
            vector<int> wanted;
            for (int value = 0; value < var.get_domain_size(); ++value) {
                if (abstract_state.contains(var_id, value) &&
                    desired_cartesian_set.test(var_id, value)) {
                    wanted.push_back(value);
                }
            }
            assert(!wanted.empty());
            splits.emplace_back(var_id, move(wanted));
        }
    }
    assert(!splits.empty());
    return splits;
}
}

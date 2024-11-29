#include "exhaustive_search.h"

#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <cassert>

using namespace std;

namespace exhaustive_search {
static bool is_strips_fact(const string &fact_name) {
    return fact_name != "<none of those>" &&
           fact_name.rfind("NegatedAtom", 0) == string::npos;
}

static vector<vector<int>> construct_and_dump_fact_mapping(
    const TaskProxy &task_proxy) {
    string atom_prefix = "Atom ";
    int num_variables = task_proxy.get_variables().size();
    vector<vector<int>> mapping(num_variables);
    int next_atom_index = 0;
    for (int var = 0; var < num_variables; ++var) {
        int domain_size = task_proxy.get_variables()[var].get_domain_size();
        mapping[var].resize(domain_size);
        for (int val = 0; val < domain_size; ++val) {
            string fact_name = task_proxy.get_variables()[var].get_fact(val).get_name();
            if (is_strips_fact(fact_name)) {
                mapping[var][val] = next_atom_index;
                cout << "F " << next_atom_index << " "
                     << fact_name.substr(atom_prefix.size()) << endl;
                ++next_atom_index;
            } else {
                mapping[var][val] = -1;
            }
        }
    }
    return mapping;
}

ExhaustiveSearch::ExhaustiveSearch()
    : SearchAlgorithm(
          ONE, numeric_limits<int>::max(), numeric_limits<double>::infinity(),
          "dump_reachable_search_space", utils::Verbosity::NORMAL) {
}

void ExhaustiveSearch::initialize() {
    utils::g_log << "Dumping the reachable state space..." << endl;
    cout << "# F (fact): [fact ID] [name]" << endl;
    cout << "# G (goal state): [goal state ID] [fact ID 1] [fact ID 2] ..." << endl;
    cout << "# N (non-goal state): [non-goal state ID] [fact ID 1] [fact ID 2] ..." << endl;
    cout << "# T (transition): [source state ID] [target state ID]" << endl;
    cout << "# The initial state has ID 0." << endl;
    fact_mapping = construct_and_dump_fact_mapping(task_proxy);
    assert(state_registry.size() <= 1);
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    // The initial state has id 0, so we'll start there.
    current_state_id = 0;
}

void ExhaustiveSearch::print_statistics() const {
    statistics.print_detailed_statistics();
    search_space.print_statistics();
}

void ExhaustiveSearch::dump_state(const State &state) const {
    char state_type = (task_properties::is_goal_state(task_proxy, state)) ? 'G' : 'N';
    cout << state_type << " " << state.get_id().value;
    for (FactProxy fact_proxy : state) {
        FactPair fact = fact_proxy.get_pair();
        int fact_id = fact_mapping[fact.var][fact.value];
        if (fact_id != -1) {
            cout << " " << fact_id;
        }
    }
    cout << endl;
}

SearchStatus ExhaustiveSearch::step() {
    if (current_state_id == static_cast<int>(state_registry.size())) {
        utils::g_log << "Finished dumping the reachable state space." << endl;
        return SOLVED;
    }

    State s = state_registry.lookup_state(StateID(current_state_id));
    statistics.inc_expanded();
    dump_state(s);

    /* Next time we'll look at the next state that was created in the registry.
       This results in a breadth-first order. */
    ++current_state_id;

    vector<OperatorID> applicable_op_ids;
    successor_generator.generate_applicable_ops(s, applicable_op_ids);

    OperatorsProxy operators = task_proxy.get_operators();
    for (OperatorID op_id : applicable_op_ids) {
        // Add successor states to registry.
        State succ_state = state_registry.get_successor_state(s, operators[op_id]);
        statistics.inc_generated();
        cout << "T " << s.get_id().value << " " << succ_state.get_id().value << endl;
    }
    return IN_PROGRESS;
}

class ExhaustiveSearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, ExhaustiveSearch> {
public:
    ExhaustiveSearchFeature() : TypedFeature("dump_reachable_search_space") {
        document_title("Exhaustive search");
        document_synopsis("Dump the reachable state space.");
    }

    virtual shared_ptr<ExhaustiveSearch> create_component(
        const plugins::Options &, const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<ExhaustiveSearch>();
    }
};

static plugins::FeaturePlugin<ExhaustiveSearchFeature> _plugin;
}

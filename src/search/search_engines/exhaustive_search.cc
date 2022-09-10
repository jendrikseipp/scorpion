#include "exhaustive_search.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace exhaustive_search {
static bool is_strips_fact(const string &fact_name) {
    return fact_name != "<none of those>" &&
           fact_name.rfind("NegatedAtom", 0) == string::npos;
}

static vector<vector<int>> construct_and_dump_fact_mapping(
    const TaskProxy &task_proxy,
    bool dump_atoms_to_file) {
    string atom_prefix = "Atom ";
    int num_variables = task_proxy.get_variables().size();
    vector<vector<int>> mapping(num_variables);
    int next_atom_index = 0;
    std::ofstream atoms_file;
    atoms_file.open("atoms.txt");
    for (int var = 0; var < num_variables; ++var) {
        int domain_size = task_proxy.get_variables()[var].get_domain_size();
        mapping[var].resize(domain_size);
        for (int val = 0; val < domain_size; ++val) {
            string fact_name = task_proxy.get_variables()[var].get_fact(val).get_name();
            if (is_strips_fact(fact_name)) {
                mapping[var][val] = next_atom_index;
                std::string normalized_atom = fact_name.substr(atom_prefix.size());
                normalized_atom.erase(std::remove_if(normalized_atom.begin(), normalized_atom.end(), [](unsigned char x){return std::isspace(x);}), normalized_atom.end());
                cout << "F " << next_atom_index << " "
                     << normalized_atom << endl;
                if (dump_atoms_to_file) {
                    // row index corresponds to atom index
                    atoms_file << normalized_atom << "\n";
                }
                ++next_atom_index;
            } else {
                mapping[var][val] = -1;
            }
        }
    }
    atoms_file.close();
    return mapping;
}

ExhaustiveSearch::ExhaustiveSearch(const Options &opts)
    : SearchEngine(opts),
      dump_atoms_to_file(opts.get<bool>("dump_atoms")),
      dump_states_to_file(opts.get<bool>("dump_states")),
      dump_transitions_to_file(opts.get<bool>("dump_transitions")) {
    assert(cost_type == ONE);
    states_file.open("states.txt");
    transitions_file.open("transitions.txt");
}

void ExhaustiveSearch::initialize() {
    utils::g_log << "Dumping the reachable state space..." << endl;
    cout << "# F (fact): [fact ID] [name]" << endl;
    cout << "# G (goal state): [goal state ID] [fact ID 1] [fact ID 2] ..." << endl;
    cout << "# N (non-goal state): [non-goal state ID] [fact ID 1] [fact ID 2] ..." << endl;
    cout << "# T (transition): [source state ID] [target state ID]" << endl;
    cout << "# The initial state has ID 0." << endl;
    fact_mapping = construct_and_dump_fact_mapping(task_proxy, dump_atoms_to_file);
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

void ExhaustiveSearch::dump_state(const State &state) {
    char state_type = (task_properties::is_goal_state(task_proxy, state)) ? 'G' : 'N';
    std::stringstream ss;
    ss << state_type << " " << state.get_id().value;
    for (FactProxy fact_proxy : state) {
        FactPair fact = fact_proxy.get_pair();
        int fact_id = fact_mapping[fact.var][fact.value];
        if (fact_id != -1) {
            ss << " " << fact_id;
        }
    }
    std::cout << ss.str() << std::endl;
    if (dump_states_to_file) {
        states_file << ss.str() << "\n";
    }
}

SearchStatus ExhaustiveSearch::step() {
    if (current_state_id == static_cast<int>(state_registry.size())) {
        utils::g_log << "Finished dumping the reachable state space." << endl;
        states_file.close();
        transitions_file.close();
        return FAILED;
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
        if (dump_transitions_to_file) {
            transitions_file << s.get_id().value << " " << succ_state.get_id().value << "\n";
        }
    }
    return IN_PROGRESS;
}

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Exhaustive search",
        "Dump the reachable state space.");
    utils::add_log_options_to_parser(parser);

    Options opts = parser.parse();

    opts.set<OperatorCost>("cost_type", ONE);
    opts.set<int>("bound", numeric_limits<int>::max());
    opts.set<double>("max_time", numeric_limits<double>::infinity());
    opts.set<bool>("dump_atoms", true);
    opts.set<bool>("dump_states", true);
    opts.set<bool>("dump_transitions", true);

    if (parser.dry_run()) {
        return nullptr;
    }

    return make_shared<ExhaustiveSearch>(opts);
}

static Plugin<SearchEngine> _plugin("dump_reachable_search_space", _parse);
}

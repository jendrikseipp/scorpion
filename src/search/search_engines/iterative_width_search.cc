#include "iterative_width_search.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <cassert>
#include <cstdlib>

using namespace std;

namespace iterative_width_search {
IterativeWidthSearch::IterativeWidthSearch(const Options &opts)
    : SearchEngine(opts),
      width(opts.get<int>("width")),
      debug(opts.get<utils::Verbosity>("verbosity") == utils::Verbosity::DEBUG),
      compute_novelty_timer(false) {
    utils::g_log << "Setting up iterative width search." << endl;
    utils::Timer setup_timer;

    fact_id_offsets.reserve(task_proxy.get_variables().size());
    int num_facts = 0;
    for (VariableProxy var : task_proxy.get_variables()) {
        fact_id_offsets.push_back(num_facts);
        int domain_size = var.get_domain_size();
        num_facts += domain_size;
    }
    utils::g_log << "Facts: " << num_facts << endl;

    seen_facts.resize(num_facts, false);
    if (width == 2) {
        seen_fact_pairs.resize(num_facts);
        // We could store only the "triangle" of values instead of the full square.
        for (int fact_id = 0; fact_id < num_facts; ++fact_id) {
            seen_fact_pairs[fact_id].resize(num_facts, false);
        }
    }

    utils::g_log << "Time for setting up iterative width search: " << setup_timer << endl;
}

void IterativeWidthSearch::initialize() {
    utils::g_log << "Starting iterative width search." << endl;
    State initial_state = state_registry.get_initial_state();
    statistics.inc_generated();
    SearchNode node = search_space.get_node(initial_state);
    node.open_initial();
    open_list.push_back(initial_state.get_id());
    compute_novelty_timer.resume();
    bool novel = is_novel(initial_state);
    compute_novelty_timer.stop();
    utils::unused_variable(novel);
    assert(novel);
}

bool IterativeWidthSearch::visit_fact_pair(int fact_id1, int fact_id2) {
    if (fact_id1 > fact_id2) {
        swap(fact_id1, fact_id2);
    }
    assert(fact_id1 < fact_id2);
    bool novel = !seen_fact_pairs[fact_id1][fact_id2];
    seen_fact_pairs[fact_id1][fact_id2] = true;
    return novel;
}

bool IterativeWidthSearch::is_novel(const State &state) {
    bool novel = false;
    for (FactProxy fact_proxy : state) {
        FactPair fact = fact_proxy.get_pair();
        int fact_id = get_fact_id(fact);
        if (!seen_facts[fact_id]) {
            seen_facts[fact_id] = true;
            novel = true;
        }
    }
    if (width == 2) {
        for (FactProxy fact_proxy1 : state) {
            FactPair fact1 = fact_proxy1.get_pair();
            int fact_id1 = get_fact_id(fact1);
            for (FactProxy fact_proxy2 : state) {
                FactPair fact2 = fact_proxy2.get_pair();
                if (fact1 == fact2) {
                    continue;
                }
                int fact_id2 = get_fact_id(fact2);
                if (visit_fact_pair(fact_id1, fact_id2)) {
                    novel = true;
                }
            }
        }
    }
    return novel;
}

bool IterativeWidthSearch::is_novel(OperatorID op_id, const State &succ_state) {
    int num_vars = fact_id_offsets.size();
    bool novel = false;
    for (EffectProxy effect : task_proxy.get_operators()[op_id].get_effects()) {
        FactPair fact = effect.get_fact().get_pair();
        int fact_id = get_fact_id(fact);
        if (!seen_facts[fact_id]) {
            seen_facts[fact_id] = true;
            novel = true;
        }
    }
    if (width == 2) {
        for (EffectProxy effect : task_proxy.get_operators()[op_id].get_effects()) {
            FactPair fact1 = effect.get_fact().get_pair();
            int fact_id1 = get_fact_id(fact1);
            for (int var2 = 0; var2 < num_vars; ++var2) {
                if (fact1.var == var2) {
                    continue;
                }
                FactPair fact2 = succ_state[var2].get_pair();
                int fact_id2 = get_fact_id(fact2);
                if (visit_fact_pair(fact_id1, fact_id2)) {
                    novel = true;
                }
            }
        }
    }
    return novel;
}

void IterativeWidthSearch::print_statistics() const {
    utils::g_log << "Time for computing novelty: " << compute_novelty_timer << endl;
    statistics.print_detailed_statistics();
    search_space.print_statistics();
}

SearchStatus IterativeWidthSearch::step() {
    if (open_list.empty()) {
        utils::g_log << "Completely explored state space -- no solution!" << endl;
        return FAILED;
    }
    StateID id = open_list.front();
    open_list.pop_front();
    State state = state_registry.lookup_state(id);
    SearchNode node = search_space.get_node(state);
    node.close();
    assert(!node.is_dead_end());
    statistics.inc_expanded();

    if (check_goal_and_set_plan(state)) {
        return SOLVED;
    }

    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(state, applicable_ops);
    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        if (node.get_real_g() + op.get_cost() >= bound) {
            continue;
        }

        State succ_state = state_registry.get_successor_state(state, op);
        statistics.inc_generated();

        compute_novelty_timer.resume();
        bool novel = is_novel(op_id, succ_state);
        compute_novelty_timer.stop();

        if (!novel) {
            continue;
        }

        SearchNode succ_node = search_space.get_node(succ_state);
        assert(succ_node.is_new());
        succ_node.open(node, op, get_adjusted_cost(op));
        open_list.push_back(succ_state.get_id());
    }

    return IN_PROGRESS;
}

void IterativeWidthSearch::dump_search_space() const {
    search_space.dump(task_proxy);
}

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Iterated width search", "");

    parser.add_option<int>(
        "width", "maximum conjunction size", "2", Bounds("1", "2"));
    SearchEngine::add_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.dry_run()) {
        return nullptr;
    }
    return make_shared<iterative_width_search::IterativeWidthSearch>(opts);
}

static Plugin<SearchEngine> _plugin("iw", _parse);
}

#include "iterative_width_search.h"

#include "goal_test.h"

#include "../../option_parser.h"
#include "../../plugin.h"
#include "../../tasks/root_task.h"
#include "../../tasks/propositional_task.h"
#include "../../task_utils/successor_generator.h"
#include "../../task_utils/task_properties.h"
#include "../../utils/logging.h"
#include "../../utils/memory.h"

#include <cassert>
#include <cstdlib>

using namespace std;
using namespace hierarchical_search_engine;

namespace iw_search {
IWSearch::IWSearch(const Options &opts)
    : HierarchicalSearchEngine(opts),
      width(opts.get<int>("width")),
      debug(opts.get<utils::Verbosity>("verbosity") == utils::Verbosity::DEBUG),
      m_novelty_base(nullptr),
      m_novelty_table(0) {
    m_name = "IWSearch";
}

bool IWSearch::is_novel(const State &state) {
    return m_novelty_table.insert(dlplan::novelty::TupleIndexGenerator(m_novelty_base, m_propositional_task->get_fact_ids(state)), true);
}

bool IWSearch::is_novel(const OperatorProxy &op, const State &succ_state) {
    return m_novelty_table.insert(dlplan::novelty::TupleIndexGenerator(m_novelty_base, m_propositional_task->get_fact_ids(op, succ_state)), true);
}

void IWSearch::print_statistics() const {
    statistics.print_detailed_statistics();
    m_search_space->print_statistics();
}

SearchStatus IWSearch::step() {
    if (open_list.empty()) {
        utils::g_log << "Completely explored state space -- no solution!" << endl;
        return FAILED;
    }
    StateID id = open_list.front();
    open_list.pop_front();
    State state = m_state_registry->lookup_state(id);
    // std::cout << "Initial state: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    SearchNode node = m_search_space->get_node(state);
    node.close();
    assert(!node.is_dead_end());
    statistics.inc_expanded();

    /* Goal check in initial state. */
    if (id == m_initial_state_id) {
        if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), state)) {
            return on_goal_leaf(state);
        }
    }

    //std::cout << m_propositional_task->compute_dlplan_state(*m_initial_state).str() << std::endl;
    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(state, applicable_ops);
    for (OperatorID op_id : applicable_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        if (node.get_real_g() + op.get_cost() >= bound) {
            continue;
        }

        State succ_state = m_state_registry->get_successor_state(state, op);
        // std::cout << "Succ state: " << m_propositional_task->compute_dlplan_state(succ_state).str() << std::endl;
        SearchNode succ_node = m_search_space->get_node(succ_state);
        if (!succ_node.is_new()) {
            continue;
        }

        statistics.inc_generated();
        bool novel = is_novel(succ_state);
        if (!novel) {
            continue;
        }

        //std::cout << m_propositional_task->compute_dlplan_state(succ_state).str() << std::endl;
        succ_node.open(node, op, get_adjusted_cost(op));
        if (width > 0) {
            open_list.push_back(succ_state.get_id());
        }

        /* Goal check after generating new node to save one g layer.*/
        if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), succ_state)) {
            return on_goal_leaf(succ_state);
        }
    }

    /* Width 0 problems must be solved after at most one expansion step. */
    if (width == 0) {
        return FAILED;
    }

    return IN_PROGRESS;
}

void IWSearch::set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task) {
    HierarchicalSearchEngine::set_propositional_task(m_propositional_task);
    m_novelty_base = std::make_shared<dlplan::novelty::NoveltyBase>(m_propositional_task->get_num_facts(), std::max(1, width));
}

void IWSearch::set_initial_state(const State& state) {
    HierarchicalSearchEngine::set_initial_state(state);
    assert(m_novelty_base);
    m_novelty_table = dlplan::novelty::NoveltyTable(m_novelty_base->get_num_tuples());
    statistics.reset();
    statistics.inc_generated();
    SearchNode node = m_search_space->get_node(state);
    node.open_initial();
    open_list.clear();
    open_list.push_back(state.get_id());
    bool novel = is_novel(state);
    utils::unused_variable(novel);
    assert(novel);
}

void IWSearch::dump_search_space() const {
    m_search_space->dump(task_proxy);
}

static shared_ptr<HierarchicalSearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Iterated width search", "");
    parser.add_option<int>(
        "width", "maximum conjunction size", "2");
    HierarchicalSearchEngine::add_child_search_engine_option(parser);
    HierarchicalSearchEngine::add_goal_test_option(parser);
    SearchEngine::add_options_to_parser(parser);

    Options opts = parser.parse();
    if (parser.dry_run()) {
        return nullptr;
    }
    return make_shared<IWSearch>(opts);
}

// ./fast-downward.py domain.pddl instance_2_1_0.pddl --translate-options --dump-predicates --dump-constants --dump-static-atoms --dump-goal-atoms --search-options --search "iw(width=2)"
static Plugin<HierarchicalSearchEngine> _plugin("iw", _parse);
}

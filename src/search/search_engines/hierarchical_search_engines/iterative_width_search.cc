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
      m_current_state_id(StateID::no_state),
      m_current_search_node(nullptr),
      m_current_op(0),
      m_novelty_base(nullptr),
      m_novelty_table(0) {
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
    if (m_current_op == m_applicable_ops.size()) {
        m_current_state_id = StateID::no_state;
    }
    std::cout << "open list: " << open_list.size() << std::endl;
    if (open_list.empty() && m_current_state_id == StateID::no_state) {
        utils::g_log << "Completely explored state space -- no solution!" << endl;
        return FAILED;
    }
    // TODO: switch from 1 expansion to 1 generate step for efficiency
    if (m_current_state_id == StateID::no_state) {
        m_current_state_id = open_list.front();
        std::cout << "popped from open list: " << m_current_state_id << std::endl;
        open_list.pop_front();
        State current_state = m_state_registry->lookup_state(m_current_state_id);
        m_current_search_node = utils::make_unique_ptr<SearchNode>(m_search_space->get_node(current_state));
        m_current_search_node->close();
        assert(!m_current_search_node->is_dead_end());
        statistics.inc_expanded();
        m_applicable_ops.clear();
        successor_generator.generate_applicable_ops(current_state, m_applicable_ops);
        m_current_op = 0;
    }
    State current_state = m_state_registry->lookup_state(m_current_state_id);
    /* Goal check in initial state. */
    if (m_current_state_id == m_initial_state_id) {
        if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), current_state)) {
            return on_goal_leaf(current_state);
        }
    }

    OperatorProxy op = task_proxy.get_operators()[m_applicable_ops[m_current_op]];
    ++m_current_op;
    std::cout << m_current_op << " " << m_applicable_ops.size() << std::endl;
    State succ_state = m_state_registry->get_successor_state(current_state, op);
    std::cout << "succ_state: " << m_propositional_task->compute_dlplan_state(succ_state).str() << std::endl;

    SearchNode succ_node = m_search_space->get_node(succ_state);
    if (!succ_node.is_new()) {
        std::cout << "is not new" << std::endl;
        return IN_PROGRESS;
    }

    statistics.inc_generated();
    bool novel = is_novel(succ_state);
    if (!novel) {
        std::cout << "is not novel" << std::endl;
        return IN_PROGRESS;
    }

    succ_node.open(*m_current_search_node, op, get_adjusted_cost(op));
    //open_list.push_back(succ_state.get_id());

    if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), succ_state)) {
        std::cout << "is goal" << std::endl;
        return on_goal_leaf(succ_state);
    } else {
        std::cout << "is not goal" << std::endl;
    }
    return IN_PROGRESS;
}

void IWSearch::set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task) {
    HierarchicalSearchEngine::set_propositional_task(m_propositional_task);
    m_novelty_base = std::make_shared<dlplan::novelty::NoveltyBase>(m_propositional_task->get_num_facts(), std::max(1, width));
}

void IWSearch::set_initial_state(const State& state) {
    std::cout << "initial state: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    HierarchicalSearchEngine::set_initial_state(state);
    assert(m_novelty_base);
    m_novelty_table = dlplan::novelty::NoveltyTable(m_novelty_base->get_num_tuples());
    statistics.reset();
    statistics.inc_generated();
    m_current_state_id = StateID::no_state;
    m_applicable_ops.clear();
    m_current_op = 0;
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

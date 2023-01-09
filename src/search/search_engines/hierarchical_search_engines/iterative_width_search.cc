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
      iterate(opts.get<bool>("iterate")),
      debug(opts.get<utils::Verbosity>("verbosity") == utils::Verbosity::DEBUG),
      m_current_state_id(StateID::no_state),
      m_initial_state_id(StateID::no_state),
      m_current_search_node(nullptr),
      m_current_op(0),
      m_novelty_base(nullptr),
      m_novelty_table(0) {
    m_name = "IWSearch";
    m_current_width = iterate ? 0 : width;
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
    if (m_current_width > width) {
        m_is_active = false;
        if (m_debug)
            std::cout << "Completely explored state space -- no solution!" << std::endl;
        return SearchStatus::FAILED;
    }

    if (m_current_op == m_applicable_ops.size()) {
        m_current_state_id = StateID::no_state;
    }

    if (open_list.empty() && m_current_state_id == StateID::no_state) {
        ++m_current_width;
        set_initial_state(m_state_registry->lookup_state(m_initial_state_id));
        return SearchStatus::IN_PROGRESS;
    }

    if (m_current_state_id == StateID::no_state) {
        m_current_state_id = open_list.front();
        open_list.pop_front();
        State current_state = m_state_registry->lookup_state(m_current_state_id);
        if (m_debug)
            std::cout << get_name() << " current_state: " << m_propositional_task->compute_dlplan_state(current_state).str() << std::endl;
        m_current_search_node = utils::make_unique_ptr<SearchNode>(m_search_space->get_node(current_state));
        m_current_search_node->close();
        assert(!m_current_search_node->is_dead_end());
        statistics.inc_expanded();
        m_applicable_ops.clear();
        successor_generator.generate_applicable_ops(current_state, m_applicable_ops);
        if (m_debug) {
            std::cout << "num applicable ops: " << m_applicable_ops.size() << std::endl;
        }
        m_current_op = 0;
    }
    State current_state = m_state_registry->lookup_state(m_current_state_id);

    SearchNode current_node = m_search_space->get_node(current_state);
    if (current_node.get_g() > m_bound) {
        m_is_active = false;
        return SearchStatus::FAILED;
    }

    /* Goal check in initial state. */
    if (m_current_state_id == m_initial_state_id) {
        if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), current_state)) {
            return on_goal(nullptr, current_state);
        }
    }

    OperatorProxy op = task_proxy.get_operators()[m_applicable_ops[m_current_op]];
    ++m_current_op;
    State succ_state = m_state_registry->get_successor_state(current_state, op);

    if (m_debug)
        std::cout << get_name() << " succ_state: " << m_propositional_task->compute_dlplan_state(succ_state).str() << std::endl;

    SearchNode succ_node = m_search_space->get_node(succ_state);
    if (!succ_node.is_new()) {
        return IN_PROGRESS;
    }

    statistics.inc_generated();
    bool novel = is_novel(succ_state);
    if (!novel) {
        return IN_PROGRESS;
    }

    // succ_node.open(*m_current_search_node, op, get_adjusted_cost(op));
    succ_node.open(*m_current_search_node, op, 1);
    if (m_current_width > 0) {
        std::cout << get_name() << " pushed: " << m_propositional_task->compute_dlplan_state(succ_state).str() << std::endl;
        open_list.push_back(succ_state.get_id());
    }

    if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), succ_state)) {
        return on_goal(nullptr, succ_state);
    }
    return IN_PROGRESS;
}

void IWSearch::set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task) {
    HierarchicalSearchEngine::set_propositional_task(m_propositional_task);
}

void IWSearch::set_initial_state(const State& state) {
    HierarchicalSearchEngine::set_initial_state(state);
    assert(m_novelty_base);
    m_novelty_base = std::make_shared<dlplan::novelty::NoveltyBase>(m_propositional_task->get_num_facts(), std::max(1, m_current_width));
    m_novelty_table = dlplan::novelty::NoveltyTable(m_novelty_base->get_num_tuples());
    statistics.reset();
    statistics.inc_generated();
    m_current_state_id = StateID::no_state;
    m_initial_state_id = state.get_id();
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

SearchStatus IWSearch::on_goal(HierarchicalSearchEngine*, const State& state) {
    if (m_debug)
        std::cout << get_name() << " on_goal: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    m_is_active = false;
    assert(m_plan.empty());
    m_search_space->trace_path(state, m_plan);
    return m_parent_search_engine->on_goal(this, state);
}

void IWSearch::dump_search_space() const {
    m_search_space->dump(task_proxy);
}

static shared_ptr<HierarchicalSearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Iterated width search", "");
    parser.add_option<int>(
        "width", "maximum conjunction size", "2");
    parser.add_option<bool>(
        "iterate", "iterate k=0,...,width", "false");
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

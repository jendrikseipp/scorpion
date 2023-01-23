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
      m_novelty_base(nullptr),
      m_novelty_table(0),
      m_search_space(nullptr) {
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

void IWSearch::reinitialize() {
    m_current_width = iterate ? 0 : width;
    m_novelty_base = std::make_shared<dlplan::novelty::NoveltyBase>(m_propositional_task->get_num_facts(), std::max(1, m_current_width));
    m_novelty_table = dlplan::novelty::NoveltyTable(m_novelty_base->get_num_tuples());
    m_search_space = utils::make_unique_ptr<SearchSpace>(*m_state_registry, utils::g_log);
}

SearchStatus IWSearch::step() {
    /* Search exhausted */
    if (m_current_width > width) {
        if (m_debug)
            std::cout << "Completely explored state space -- no solution!" << std::endl;
        return SearchStatus::FAILED;
    }

    /* Restart search and increment width bound. */
    if (open_list.empty()) {
        ++m_current_width;
        std::cout << "current_width: " << m_current_width << std::endl;
        set_initial_state(m_state_registry->lookup_state(m_initial_state_id));
        return SearchStatus::IN_PROGRESS;
    }

    /* Pop node from queue */
    StateID id = open_list.front();
    open_list.pop_front();
    State state = m_state_registry->lookup_state(id);
    if (m_debug)
        std::cout << get_name() << " state: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    SearchNode node = m_search_space->get_node(state);
    node.close();
    assert(!node.is_dead_end());
    statistics.inc_expanded();

    /* Search exhausted by bound. */
    if (node.get_g() > m_bound) {
        return SearchStatus::FAILED;
    }

    /* Goal check in initial state of subproblem. */
    if (id == m_initial_state_id) {
        if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), state)) {
            m_solution = IWSearchSolution{{}, state.get_id(), m_current_width};
            return SearchStatus::SOLVED;
        }
    }

    /* Generate successors */
    vector<OperatorID> applicable_ops;
    successor_generator.generate_applicable_ops(state, applicable_ops);
    for (auto op_id : applicable_ops) {
        OperatorProxy op = task_proxy.get_operators()[op_id];
        State succ_state = m_state_registry->get_successor_state(state, op);
        if (m_debug)
            std::cout << get_name() << " succ_state: " << m_propositional_task->compute_dlplan_state(succ_state).str() << std::endl;

        SearchNode succ_node = m_search_space->get_node(succ_state);
        if (!succ_node.is_new()) {
            continue;
        }

        succ_node.open(node, op, 1);
        statistics.inc_generated();

        if (width > 0) {
            bool novel = is_novel(succ_state);
            if (!novel) {
                continue;
            }
            open_list.push_back(succ_state.get_id());
        }

        if (m_goal_test->is_goal(m_state_registry->lookup_state(m_initial_state_id), succ_state)) {
            if (m_debug)
                std::cout << get_name() << " goal_state: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;

            // set the solution.
            Plan plan;
            m_search_space->trace_path(succ_state, plan);
            m_solution = IWSearchSolution{plan, succ_state.get_id(), m_current_width};
            return SearchStatus::SOLVED;
        }
    }
    return IN_PROGRESS;
}

void IWSearch::set_state_registry(std::shared_ptr<StateRegistry> state_registry) {
    HierarchicalSearchEngine::set_state_registry(state_registry);
    m_search_space = utils::make_unique_ptr<SearchSpace>(*m_state_registry, utils::g_log);
}

void IWSearch::set_propositional_task(std::shared_ptr<extra_tasks::PropositionalTask> m_propositional_task) {
    HierarchicalSearchEngine::set_propositional_task(m_propositional_task);
}

void IWSearch::set_initial_state(const State& state) {
    HierarchicalSearchEngine::set_initial_state(state);

    m_novelty_base = std::make_shared<dlplan::novelty::NoveltyBase>(m_propositional_task->get_num_facts(), std::max(1, m_current_width));
    m_novelty_table = dlplan::novelty::NoveltyTable(m_novelty_base->get_num_tuples());
    m_search_space = utils::make_unique_ptr<SearchSpace>(*m_state_registry, utils::g_log);

    statistics.inc_generated();
    m_initial_state_id = state.get_id();
    SearchNode node = m_search_space->get_node(state);
    node.open_initial();
    open_list.clear();
    open_list.push_back(state.get_id());
    bool novel = is_novel(state);
    utils::unused_variable(novel);
    assert(novel);
}

SearchStatistics IWSearch::collect_statistics() const {
    return statistics;
}

void IWSearch::dump_search_space() const {
    m_search_space->dump(task_proxy);
}

IWSearchSolutions IWSearch::get_partial_solutions() const {
    return {m_solution,};
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

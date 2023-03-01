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
#include "../../utils/timer.h"


#include <cassert>
#include <cstdlib>

using namespace std;


namespace hierarchical_search_engine {
IWSearch::IWSearch(const Options &opts)
    : HierarchicalSearchEngine(opts),
      m_width(opts.get<int>("width")),
      m_iterate(opts.get<bool>("iterate")),
      m_novelty_table(task_proxy, 0, nullptr),
      m_search_space(nullptr) {
    m_name = "IWSearch";
    m_current_width = m_iterate ? 0 : m_width;
}

bool IWSearch::is_novel(const State &state) {
    // return m_novelty_table.compute_novelty_and_update_table(m_propositional_task->get_propositions(state)) < 3;
    return m_novelty_table.compute_novelty_and_update_table(state) < 3;
}

bool IWSearch::is_novel(const OperatorProxy& op, const State &state) {
    // return m_novelty_table.compute_novelty_and_update_table(m_propositional_task->get_propositions(op, state)) < 3;
    return m_novelty_table.compute_novelty_and_update_table(op, state) < 3;
}

void IWSearch::print_statistics() const {
    statistics.print_detailed_statistics();
    m_search_space->print_statistics();
}

void IWSearch::reinitialize() {
    HierarchicalSearchEngine::reinitialize();
    m_current_width = m_iterate ? 0 : m_width;
}

SearchStatus IWSearch::step() {
    /* Search exhausted */
    if (m_current_width > m_width) {
        if (m_debug)
            std::cout << "Completely explored state space -- no solution!" << std::endl;
        return SearchStatus::FAILED;
    }

    /* Restart search and increment m_width bound. */
    if (m_open_list.empty()) {
        ++m_current_width;
        set_initial_state(m_state_registry->lookup_state(m_initial_state_id));
        return SearchStatus::IN_PROGRESS;
    }

    /* Pop node from queue */
    StateID id = m_open_list.front();
    m_open_list.pop_front();
    State state = m_state_registry->lookup_state(id);
    if (m_debug)
        std::cout << get_name() << " state: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;
    SearchNode node = m_search_space->get_node(state);
    node.close();
    assert(!node.is_dead_end());
    statistics.inc_expanded();
    // std::cout << get_name() << " expanded: " << m_propositional_task->compute_dlplan_state(state).str() << std::endl;

    /* Search exhausted by bound. */
    if (node.get_g() > m_bound) {
        return SearchStatus::FAILED;
    }

    /* Goal check in initial state of subproblem. */
    if (id == m_initial_state_id) {
        if (is_goal(state)) {
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
        if (m_current_width > 0) {
            bool novel = is_novel(op, succ_state);
            if (!novel) {
                continue;
            }
            m_open_list.push_back(succ_state.get_id());
        }
        statistics.inc_generated();

        if (is_goal(succ_state)) {
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

bool IWSearch::set_initial_state(const State& state) {
    m_novelty_table = novelty::NoveltyTable(task_proxy, m_current_width, m_propositional_task, m_propositional_task->get_fact_indexer());
    m_search_space = utils::make_unique_ptr<SearchSpace>(*m_state_registry, utils::g_log);

    statistics.inc_generated();
    m_initial_state_id = state.get_id();
    SearchNode node = m_search_space->get_node(state);
    node.open_initial();
    m_open_list.clear();
    m_open_list.push_back(state.get_id());
    bool novel = is_novel(state);
    utils::unused_variable(novel);
    assert(novel);
    return m_goal_test->set_initial_state(state);
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

static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Iterated width search", "");
    parser.add_option<int>(
        "width", "maximum conjunction size", "2", Bounds("0", "2"));
    parser.add_option<bool>(
        "iterate", "iterate k=0,...,width", "true");
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
static Plugin<SearchEngine> _plugin("iw", _parse);
}

#include "flaw_search.h"

#include "../evaluators/g_evaluator.h"
#include "../open_lists/best_first_open_list.h"
#include "../utils/logging.h"


using namespace std;

namespace cegar {
FlawSearch::FlawSearch(const std::shared_ptr<AbstractTask> &task) :
    task_proxy(*task),
    open_list(nullptr),
    state_registry(nullptr),
    statistics(nullptr) {
    shared_ptr<Evaluator> g_evaluator = make_shared<g_evaluator::GEvaluator>();
    Options options;
    options.set("eval", g_evaluator);
    options.set("pref_only", false);

    open_list = make_shared<standard_scalar_open_list::BestFirstOpenListFactory>(options)->create_state_open_list();
}

void FlawSearch::initialize() {
    open_list->clear();
    cout << "open list empty: " << open_list->empty() << endl;
    state_registry = utils::make_unique_ptr<StateRegistry>(task_proxy);
    statistics = utils::make_unique_ptr<SearchStatistics>(utils::Verbosity::SILENT);
    State initial_state = state_registry->get_initial_state();
    EvaluationContext eval_context(initial_state, 0, true, statistics.get());
    open_list->insert(eval_context, initial_state.get_id());
    cout << "open list empty: " << open_list->empty() << endl;
}

SearchStatus FlawSearch::step() {
}

void FlawSearch::search_for_flaws(const AbstractSearch *abstract_search) {
    initialize();
}
}

#include "novelty_evaluator.h"

#include "../evaluation_context.h"

#include "../plugins/plugin.h"
#include "../utils/logging.h"
#include "../utils/markup.h"

#include <iostream>

using namespace std;

namespace novelty {
NoveltyEvaluator::NoveltyEvaluator(
    int width, const vector<shared_ptr<Evaluator>> &evals,
    bool consider_only_novel_states, const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description, utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      width(width),
      consider_only_novel_states(consider_only_novel_states),
      evals(evals),
      task_info(task_proxy),
      novelty_to_num_states(NoveltyTable::UNKNOWN_NOVELTY, 0) {
    use_for_reporting_minima = false;
    use_for_boosting = false;
    if (log.is_at_least_debug()) {
        log << "Initializing novelty evaluator..." << endl;
    }
    if (!does_cache_estimates()) {
        cerr << "NoveltyEvaluator needs cache_estimates=true" << endl;
        utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
    }
}

NoveltyEvaluator::~NoveltyEvaluator() {
    log << "Num states per novelty: " << novelty_to_num_states << endl;
}

void NoveltyEvaluator::set_novelty(const State &state, int novelty) {
    assert(heuristic_cache[state].dirty);
    if (consider_only_novel_states &&
        novelty == NoveltyTable::UNKNOWN_NOVELTY) {
        novelty = DEAD_END;
    }
    heuristic_cache[state].h = novelty;
    heuristic_cache[state].dirty = false;
}

void NoveltyEvaluator::get_path_dependent_evaluators(set<Evaluator *> &evals) {
    evals.insert(this);
    for (auto &evaluator : this->evals) {
        evaluator->get_path_dependent_evaluators(evals);
    }
}

vector<int> NoveltyEvaluator::evaluate_state(const State &state) {
    state.unpack();
    EvaluationContext eval_context(state);
    vector<int> eval_values;
    eval_values.reserve(evals.size());
    for (const shared_ptr<Evaluator> &eval : evals) {
        int value = eval_context.get_evaluator_value_or_infinity(eval.get());
        eval_values.push_back(value);
    }
    return eval_values;
}

void NoveltyEvaluator::notify_initial_state(const State &initial_state) {
    vector<int> eval_values = evaluate_state(initial_state);
    log << "Evaluator values for initial state: " << eval_values << endl;
    // For the initial state, the table should have no entry.
    assert(!novelty_tables.contains(eval_values));
    novelty_tables.emplace(eval_values, NoveltyTable(width, task_info));
    int novelty = novelty_tables.at(eval_values)
                      .compute_novelty_and_update_table(initial_state);
    set_novelty(initial_state, novelty);
}

void NoveltyEvaluator::notify_state_transition(
    const State &parent, OperatorID op_id, const State &state) {
    // Only compute novelty for new states.
    if (heuristic_cache[state].dirty) {
        vector<int> eval_values = evaluate_state(state);
        auto it = novelty_tables.find(eval_values);
        if (it == novelty_tables.end()) {
            it = novelty_tables.emplace_hint(
                it, eval_values, NoveltyTable(width, task_info));
        }
        int novelty = -1;
        // Use shortcut when the two states belong to the same partition.
        if (evaluate_state(parent) == eval_values) {
            novelty = it->second.compute_novelty_and_update_table(
                parent, op_id.get_index(), state);
        } else {
            novelty = it->second.compute_novelty_and_update_table(state);
        }
        ++novelty_to_num_states[novelty - 1];
        set_novelty(state, novelty);
    }
}

bool NoveltyEvaluator::dead_ends_are_reliable() const {
    return false;
}

int NoveltyEvaluator::compute_heuristic(const State &) {
    ABORT("Novelty should already be stored in heuristic cache.");
}

class NoveltyEvaluatorFeature
    : public plugins::TypedFeature<Evaluator, NoveltyEvaluator> {
public:
    NoveltyEvaluatorFeature() : TypedFeature("novelty") {
        document_title("Novelty evaluator");
        document_synopsis(
            "Computes the novelty w(s) of a state s given the partition functions "
            "evals=⟨h_1, ..., h_n⟩ as the size of the smallest set of atoms A such "
            "that s is the first evaluated state that subsumes A, among "
            "all states s' visited before s for which h_i(s) = h_i(s') for 1 ≤ i ≤ n. "
            "Best-First Width Search (BFWS) was introduced in " +
            utils::format_conference_reference(
                {"Nir Lipovetzky", "Hector Geffner"},
                "Best-First Width Search: Exploration and Exploitation in Classical Planning",
                "https://ojs.aaai.org/index.php/AAAI/article/view/11027/10886",
                "Proceedings of the Thirty-First AAAI Conference on Artificial Intelligence (AAAI-17)",
                "3590-3596", "AAAI Press", "2017") +
            "and BFWS was integrated into Scorpion in" +
            utils::format_conference_reference(
                {"Augusto B. Corrêa", "Jendrik Seipp"},
                "Alternation-Based Novelty Search",
                "https://mrlab.ai/papers/correa-seipp-icaps2025.pdf",
                "Proceedings of the 35th International Conference on Automated "
                "Planning and Scheduling (ICAPS 2025)",
                "to appear", "AAAI Press", "2025"));

        add_option<int>(
            "width", "maximum conjunction size", "2",
            plugins::Bounds("1", "2"));
        add_list_option<shared_ptr<Evaluator>>(
            "evals", "evaluators", "[const()]");
        add_option<bool>(
            "consider_only_novel_states", "assign infinity to non-novel states",
            "true");
        add_option<int>(
            "max_variables_for_width2",
            "if there are more variables, use width=1", "100",
            plugins::Bounds("0", "infinity"));

        add_heuristic_options_to_feature(*this, "novelty");

        document_language_support("action costs", "ignored by design");
        document_language_support("conditional effects", "supported");
        document_language_support("axioms", "supported");

        document_property("admissible", "no");
        document_property("consistent", "no");
        document_property("safe", "if consider_only_novel_states=false");
        document_property("preferred operators", "no");
    }

    virtual shared_ptr<NoveltyEvaluator> create_component(
        const plugins::Options &opts) const override {
        int width = opts.get<int>("width");
        int num_vars =
            TaskProxy(*opts.get<shared_ptr<AbstractTask>>("transform"))
                .get_variables()
                .size();
        if (num_vars > opts.get<int>("max_variables_for_width2")) {
            utils::g_log << "Number of variables exceeds limit "
                         << " --> use width=1" << endl;
            width = 1;
        }
        return plugins::make_shared_from_arg_tuples<NoveltyEvaluator>(
            width, opts.get_list<shared_ptr<Evaluator>>("evals"),
            opts.get<bool>("consider_only_novel_states"),
            get_heuristic_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<NoveltyEvaluatorFeature> _plugin;
}

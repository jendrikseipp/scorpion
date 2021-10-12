#include "comparison_evaluator.h"

#include "../option_parser.h"
#include "../plugin.h"

#include "../evaluation_context.h"
#include "../evaluation_result.h"
#include "../utils/logging.h"



using namespace std;

namespace comparison_evaluator {
ComparisonEvaluator::ComparisonEvaluator(const Options &opts)
    :evaluators(opts.get_list<shared_ptr<Evaluator>>("evals")),
    handling(opts.get<UnequalityHandling>("uneq_handling")),
    c_options(opts.get<CombineOptions>("combine")) {

    }

ComparisonEvaluator::~ComparisonEvaluator(){

}

EvaluationResult ComparisonEvaluator::compute_result(EvaluationContext &eval_context) {
    EvaluationResult result;

    vector<int> values;
    values.reserve(evaluators.size());

    // Collect component values.
    for (const shared_ptr<Evaluator> &evaluator : evaluators) {
        int value = eval_context.get_evaluator_value_or_infinity(evaluator.get());
        if (value == EvaluationResult::INFTY) {
            result.set_evaluator_value(value);
            return result;
        } else {
            values.push_back(value);
        }
    }

    //Check equality
    int lastValue = -1;
    bool all_equal = true;
    for (int value: values) {
        if (lastValue == -1) {
            lastValue = value;
        } else {
            if (lastValue != value){
                all_equal = false;
                break;
            }
        }
    }

    if (!all_equal) {
        eval_context.get_state().unpack();
        if (handling == UnequalityHandling::PRINT) {
            cout << "Unequality: " << values << " in state: " << eval_context.get_state().get_unpacked_values() << endl;
        } else if (handling == UnequalityHandling::EXCEPTION) {
            cerr << "Unequality: " << values << " in state: " << eval_context.get_state().get_unpacked_values() << endl;
            utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
        }
    }

    if (c_options == CombineOptions::MAX) {
        result.set_evaluator_value(*max_element(values.begin(),values.end()));
    } else if (c_options == CombineOptions::MIN) {
        result.set_evaluator_value(*min_element(values.begin(),values.end()));
    }

    return result;
}

void ComparisonEvaluator::get_path_dependent_evaluators(
    set<Evaluator *> &evals) {
    for (auto &subevaluator : evaluators)
        subevaluator->get_path_dependent_evaluators(evals);
}

static shared_ptr<Evaluator> _parse(OptionParser &parser) {
    parser.document_synopsis(
        "Max evaluator",
        "Calculates the maximum of the sub-evaluators.");
    parser.add_list_option<shared_ptr<Evaluator>>(
        "evals",
        "at least one evaluator");
    parser.add_enum_option<CombineOptions>(
        "combine",
        {"MAX","MIN"},
        "how to combine the evaluator values",
        "MIN");
    parser.add_enum_option<UnequalityHandling>(
        "uneq_handling",
        {"PRINT","EXCEPTION"},
        "how to behave on found inequalities",
        "PRINT");

    Options opts = parser.parse();

    opts.verify_list_non_empty<shared_ptr<Evaluator>>("evals");

    if (parser.dry_run()) {
        return nullptr;
    }
    return make_shared<ComparisonEvaluator>(opts);
}

static Plugin<Evaluator> plugin("compare", _parse, "evaluators_basic");
}

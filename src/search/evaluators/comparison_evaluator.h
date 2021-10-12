#ifndef EVALUATORS_COMPARISON_EVALUATOR_H
#define EVALUATORS_COMPARISON_EVALUATOR_H


#include "../evaluator.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace options {
class Options;
}

enum class UnequalityHandling {
    PRINT,
    EXCEPTION
};

namespace comparison_evaluator {

class ComparisonEvaluator : public Evaluator {
    private:
        std::vector<std::shared_ptr<Evaluator>> evaluators;
        const UnequalityHandling handling;
    
public:
    explicit ComparisonEvaluator(const options::Options &opts);
    virtual ~ComparisonEvaluator() override;

    EvaluationResult compute_result(
        EvaluationContext &eval_context) override;

    void get_path_dependent_evaluators(
        std::set<Evaluator *> &evals) override;
};
}

#endif
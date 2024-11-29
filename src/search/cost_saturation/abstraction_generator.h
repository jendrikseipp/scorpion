#ifndef COST_SATURATION_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_ABSTRACTION_GENERATOR_H

#include "types.h"

#include "../utils/logging.h"

#include <memory>

class AbstractTask;

namespace cost_saturation {
class AbstractionGenerator {
protected:
    mutable utils::LogProxy log;

public:
    explicit AbstractionGenerator(utils::Verbosity verbosity);
    virtual ~AbstractionGenerator() = default;

    virtual Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends) = 0;
};

extern void add_abstraction_generator_arguments_to_feature(plugins::Feature &feature);

extern std::tuple<utils::Verbosity> get_abstraction_generator_arguments_from_options(
    const plugins::Options &opts);
}

#endif

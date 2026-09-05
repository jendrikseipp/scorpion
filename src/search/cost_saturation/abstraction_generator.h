#ifndef COST_SATURATION_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_ABSTRACTION_GENERATOR_H

#include "types.h"

#include "../component.h"

#include "../utils/logging.h"

#include <memory>

class AbstractTask;

namespace cost_saturation {
class AbstractionGenerator : public components::TaskSpecificComponent {
protected:
    mutable utils::LogProxy log;

public:
    AbstractionGenerator(
        const std::shared_ptr<AbstractTask> &task, utils::Verbosity verbosity);

    virtual Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task, DeadEnds *dead_ends) = 0;
};

using TaskIndependentAbstractionGenerator =
    components::TaskIndependentComponent<AbstractionGenerator>;

extern void add_abstraction_generator_arguments_to_feature(
    plugins::Feature &feature);

extern std::tuple<utils::Verbosity>
get_abstraction_generator_arguments_from_options(const plugins::Options &opts);
}

#endif

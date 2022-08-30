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
    explicit AbstractionGenerator(const options::Options &opts);
    virtual ~AbstractionGenerator() = default;

    virtual Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends) = 0;
};
}

#endif

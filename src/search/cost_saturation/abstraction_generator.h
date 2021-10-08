#ifndef COST_SATURATION_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_ABSTRACTION_GENERATOR_H

#include "types.h"

#include <memory>

class AbstractTask;

namespace cost_saturation {
class AbstractionGenerator {
public:
    virtual Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task,
        DeadEnds *dead_ends) = 0;

    virtual ~AbstractionGenerator() = default;
};
}

#endif

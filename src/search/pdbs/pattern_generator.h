#ifndef PDBS_PATTERN_GENERATOR_H
#define PDBS_PATTERN_GENERATOR_H

#include "pattern_collection_information.h"
#include "pattern_information.h"
#include "types.h"

#include <functional>
#include <memory>

class AbstractTask;

namespace pdbs {
using PatternHandler = std::function<bool (const Pattern &)>;
class PatternCollectionGenerator {
protected:
    PatternHandler handle_pattern;
public:
    virtual ~PatternCollectionGenerator() = default;

    virtual PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task) = 0;
};

class PatternGenerator {
public:
    virtual ~PatternGenerator() = default;

    virtual PatternInformation generate(const std::shared_ptr<AbstractTask> &task) = 0;
};
}

#endif

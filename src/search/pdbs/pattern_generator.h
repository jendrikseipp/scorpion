#ifndef PDBS_PATTERN_GENERATOR_H
#define PDBS_PATTERN_GENERATOR_H

#include "pattern_collection_information.h"
#include "pattern_information.h"
#include "types.h"

#include "../utils/logging.h"

#include <functional>
#include <memory>
#include <string>

class AbstractTask;

namespace options {
class OptionParser;
class Options;
}

namespace utils {
class RandomNumberGenerator;
}

namespace pdbs {
using PatternHandler = std::function<bool (const Pattern &)>;
class PatternCollectionGenerator {
    virtual std::string name() const = 0;
    virtual PatternCollectionInformation compute_patterns(
        const std::shared_ptr<AbstractTask> &task) = 0;
protected:
    mutable utils::LogProxy log;
    PatternHandler handle_pattern;
    DeadEnds *dead_ends;
public:
    explicit PatternCollectionGenerator(const options::Options &opts);
    virtual ~PatternCollectionGenerator() = default;

    PatternCollectionInformation generate(
        const std::shared_ptr<AbstractTask> &task);

    void set_dead_ends_store(DeadEnds *dead_ends) {
        this->dead_ends = dead_ends;
    }
};

class PatternGenerator {
    virtual std::string name() const = 0;
    virtual PatternInformation compute_pattern(
        const std::shared_ptr<AbstractTask> &task) = 0;
protected:
    mutable utils::LogProxy log;
public:
    explicit PatternGenerator(const options::Options &opts);
    virtual ~PatternGenerator() = default;

    PatternInformation generate(const std::shared_ptr<AbstractTask> &task);
};

extern void add_generator_options_to_parser(options::OptionParser &parser);
}

#endif

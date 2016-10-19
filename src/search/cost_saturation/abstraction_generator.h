#ifndef COST_SATURATION_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_ABSTRACTION_GENERATOR_H

#include <memory>
#include <vector>

class AbstractTask;
class State;

namespace cost_saturation {
class Abstraction;

using StateMap = std::function<int (const State &)>;
using AbstractionAndStateMap = std::pair<std::unique_ptr<Abstraction>, StateMap>;

class AbstractionGenerator {
public:
    virtual std::vector<AbstractionAndStateMap> generate_abstractions(
        const std::shared_ptr<AbstractTask> &task) = 0;
};
}

#endif

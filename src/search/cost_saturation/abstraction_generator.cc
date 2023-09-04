#include "abstraction_generator.h"

#include "../plugins/plugin.h"

using namespace std;

namespace cost_saturation {
AbstractionGenerator::AbstractionGenerator(const plugins::Options &opts)
    : log(utils::get_log_from_options(opts)) {
}

static class AbstractionGeneratorCategoryPlugin : public plugins::TypedCategoryPlugin<AbstractionGenerator> {
public:
    AbstractionGeneratorCategoryPlugin() : TypedCategoryPlugin("AbstractionGenerator") {
        document_synopsis("Create abstractions for cost partitioning heuristics.");
    }
}
_category_plugin;
}

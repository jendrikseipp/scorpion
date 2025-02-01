#include "abstraction_generator.h"

#include "../plugins/plugin.h"

using namespace std;

namespace cost_saturation {
AbstractionGenerator::AbstractionGenerator(utils::Verbosity verbosity)
    : log(utils::get_log_for_verbosity(verbosity)) {
}

void add_abstraction_generator_arguments_to_feature(plugins::Feature &feature) {
    utils::add_log_options_to_feature(feature);
}

tuple<utils::Verbosity> get_abstraction_generator_arguments_from_options(
    const plugins::Options &opts) {
    return tuple_cat(utils::get_log_arguments_from_options(opts));
}

static class AbstractionGeneratorCategoryPlugin : public plugins::TypedCategoryPlugin<AbstractionGenerator> {
public:
    AbstractionGeneratorCategoryPlugin() : TypedCategoryPlugin("AbstractionGenerator") {
        document_synopsis("Create abstractions for cost partitioning heuristics.");
    }
}
_category_plugin;
}

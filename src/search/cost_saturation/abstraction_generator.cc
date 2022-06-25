#include "abstraction_generator.h"

#include "../plugin.h"

using namespace std;

namespace cost_saturation {
AbstractionGenerator::AbstractionGenerator(const options::Options &opts)
    : log(utils::get_log_from_options(opts)) {
}

static PluginTypePlugin<AbstractionGenerator> _type_plugin(
    "AbstractionGenerator",
    "");
}

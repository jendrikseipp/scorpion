#include "abstraction_generator.h"

#include "../plugin.h"

using namespace std;

namespace cost_saturation {
static PluginTypePlugin<AbstractionGenerator> _type_plugin(
    "AbstractionGenerator",
    // TODO: Replace empty string by synopsis for the wiki page.
    "");
}

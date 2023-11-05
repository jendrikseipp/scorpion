#include "../plugins/plugin.h"

namespace cost_saturation_plugin_group {
static class CostSaturationGroupPlugin : public plugins::SubcategoryPlugin {
public:
    CostSaturationGroupPlugin() : SubcategoryPlugin("heuristics_cost_partitioning") {
        document_title("Cost Partitioning Heuristics");
    }
}
_subcategory_plugin;
}

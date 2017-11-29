#include "cost_partitioning_generator.h"

#include "../plugin.h"

using namespace std;

namespace cost_saturation {
static PluginTypePlugin<CostPartitioningGenerator> _type_plugin(
    "CostPartitioningGenerator",
    "Cost partitioning generator.");
}

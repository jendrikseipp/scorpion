#include "state_registry.h"

#include "plugins/plugin.h"

static plugins::TypedEnumPlugin<StateRegistryType> _enum_plugin({
    {"packed", "state variables are packed into integers which are stored in a segmented vector"},
    {"unpacked", "state variables are stored in a segmented vector"},
    {"tree_packed", "state variables are packed into integers which are stored in a tree structure"},
    {"tree_unpacked", "state variables are stored in a tree structure"},
    {"fixed_tree_unpacked", "state variables are stored in a tree structure with fixed size"},
    {"fixed_tree_packed", "state variables are stored in a tree structure with fixed size"},
});


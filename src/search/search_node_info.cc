#include "search_node_info.h"

static_assert(
    sizeof(SearchNodeInfo) == 2 * sizeof(int) + sizeof(StateID),
    "The size of SearchNodeInfo is larger than expected. This probably means "
    "that packing two fields into one integer using bitfields is not supported.");

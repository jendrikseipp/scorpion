#include "fact_indexer.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

using namespace std;

namespace novelty {
FactIndexer::FactIndexer(const TaskProxy &task_proxy) {
    fact_offsets.reserve(task_proxy.get_variables().size());
    num_facts = 0;
    for (VariableProxy var : task_proxy.get_variables()) {
        fact_offsets.push_back(num_facts);
        int domain_size = var.get_domain_size();
        num_facts += domain_size;
    }

    int num_vars = task_proxy.get_variables().size();
    int last_domain_size = task_proxy.get_variables()[num_vars - 1].get_domain_size();
    // We don't need offsets for facts of the last variable.
    int num_pair_offsets = num_facts - last_domain_size;
    pair_offsets.reserve(num_pair_offsets);
    int current_pair_offset = 0;
    int num_facts_in_higher_vars = num_facts;
    num_pairs = 0;
    for (int var_id = 0; var_id < num_vars - 1; ++var_id) {  // Skip last var.
        int domain_size = task_proxy.get_variables()[var_id].get_domain_size();
        int var_last_fact_id = get_fact_id(FactPair(var_id, domain_size - 1));
        num_facts_in_higher_vars -= domain_size;
        num_pairs += (domain_size * num_facts_in_higher_vars);
        for (int value = 0; value < domain_size; ++value) {
            pair_offsets.push_back(current_pair_offset - (var_last_fact_id + 1));
            current_pair_offset += num_facts_in_higher_vars;
        }
    }
    assert(static_cast<int>(pair_offsets.size()) == num_pair_offsets);
    assert(num_facts_in_higher_vars == last_domain_size);
}

}

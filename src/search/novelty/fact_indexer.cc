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
}

}

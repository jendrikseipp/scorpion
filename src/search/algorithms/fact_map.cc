#include "fact_map.h"

using namespace std;

namespace fact_map {
FactMap::FactMap(const vector<int> &domain_sizes, int default_value) {
    fact_offsets.reserve(domain_sizes.size());
    int num_facts = 0;
    for (int domain_size : domain_sizes) {
        fact_offsets.push_back(num_facts);
        num_facts += domain_size;
    }
    values.resize(num_facts, default_value);
}
}

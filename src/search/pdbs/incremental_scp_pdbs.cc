#include "incremental_scp_pdbs.h"

#include "pattern_database.h"

#include <limits>

using namespace std;

namespace pdbs {
IncrementalSCPPDBs::IncrementalSCPPDBs(const TaskProxy &task_proxy)
    : IncrementalPDBs(task_proxy) {
}

void IncrementalSCPPDBs::add_pdb(const shared_ptr<PatternDatabase> &pdb) {
    patterns->push_back(pdb->get_pattern());
    pattern_databases->push_back(pdb);
    size += pattern_databases->back()->get_size();
}

int IncrementalSCPPDBs::get_value(const State &state) const {
    int sum_h = 0;
    for (const shared_ptr<PatternDatabase> &pdb : *pattern_databases) {
        int h = pdb->get_value(state);
        if (h == numeric_limits<int>::max()) {
            return h;
        } else {
            sum_h += h;
        }
    }
    return sum_h;
}

bool IncrementalSCPPDBs::is_dead_end(const State &state) const {
    return get_value(state) == numeric_limits<int>::max();
}

PatternCollectionInformation
IncrementalSCPPDBs::get_pattern_collection_information() const {
    PatternCollectionInformation result(task_proxy, patterns);
    result.set_pdbs(pattern_databases);
    return result;
}
}

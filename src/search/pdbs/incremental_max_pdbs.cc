#include "incremental_max_pdbs.h"

#include "pattern_database.h"

#include <limits>

using namespace std;

namespace pdbs {
IncrementalMaxPDBs::IncrementalMaxPDBs(const TaskProxy &task_proxy)
    : IncrementalPDBs(task_proxy) {
}

void IncrementalMaxPDBs::add_pdb(const shared_ptr<PatternDatabase> &pdb) {
    patterns->push_back(pdb->get_pattern());
    pattern_databases->push_back(pdb);
    size += pattern_databases->back()->get_size();
}

int IncrementalMaxPDBs::get_value(const State &state) const {
    int max_h = 0;
    for (const shared_ptr<PatternDatabase> &pdb : *pattern_databases) {
        int h = pdb->get_value(state);
        if (h == numeric_limits<int>::max()) {
            return h;
        } else {
            max_h = max(h, max_h);
        }
    }
    return max_h;
}

bool IncrementalMaxPDBs::is_dead_end(const State &state) const {
    return get_value(state) == numeric_limits<int>::max();
}

PatternCollectionInformation
IncrementalMaxPDBs::get_pattern_collection_information() const {
    PatternCollectionInformation result(task_proxy, patterns);
    result.set_pdbs(pattern_databases);
    return result;
}
}

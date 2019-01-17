#include "incremental_pdbs.h"

#include "pattern_database.h"

using namespace std;

namespace pdbs {
IncrementalPDBs::IncrementalPDBs(
    const TaskProxy &task_proxy, const PatternCollection &intitial_patterns)
    : task_proxy(task_proxy),
      patterns(make_shared<PatternCollection>(intitial_patterns.begin(),
                                              intitial_patterns.end())),
      pattern_databases(make_shared<PDBCollection>()),
      size(0) {
    pattern_databases->reserve(patterns->size());
    for (const Pattern &pattern : *patterns)
        add_pdb_for_pattern(pattern);
}

void IncrementalPDBs::add_pdb_for_pattern(const Pattern &pattern) {
    pattern_databases->push_back(make_shared<PatternDatabase>(task_proxy, pattern));
    size += pattern_databases->back()->get_size();
}
}

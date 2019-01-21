#include "incremental_pdbs.h"

#include "pattern_database.h"

using namespace std;

namespace pdbs {
IncrementalPDBs::IncrementalPDBs(const TaskProxy &task_proxy)
    : task_proxy(task_proxy),
      patterns(make_shared<PatternCollection>()),
      pattern_databases(make_shared<PDBCollection>()),
      size(0) {
}
}

#ifndef PDBS_INCREMENTAL_MAX_PDBS_H
#define PDBS_INCREMENTAL_MAX_PDBS_H

#include "pattern_collection_information.h"
#include "types.h"

#include "../task_proxy.h"

#include <memory>

namespace pdbs {
class IncrementalMaxPDBs {
    TaskProxy task_proxy;

    std::shared_ptr<PatternCollection> patterns;
    std::shared_ptr<PDBCollection> pattern_databases;

    // The sum of all abstract state sizes of all pdbs in the collection.
    int size;

    // Adds a PDB for pattern.
    void add_pdb_for_pattern(const Pattern &pattern);
public:
    IncrementalMaxPDBs(const TaskProxy &task_proxy,
                       const PatternCollection &intitial_patterns);

    // Adds a new PDB to the collection.
    void add_pdb(const std::shared_ptr<PatternDatabase> &pdb);

    int get_value(const State &state) const;

    bool is_dead_end(const State &state) const;

    PatternCollectionInformation get_pattern_collection_information() const;

    std::shared_ptr<PDBCollection> get_pattern_databases() const {
        return pattern_databases;
    }

    int get_size() const {
        return size;
    }
};
}

#endif

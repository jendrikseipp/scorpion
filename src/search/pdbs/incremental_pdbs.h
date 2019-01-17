#ifndef PDBS_INCREMENTAL_PDBS_H
#define PDBS_INCREMENTAL_PDBS_H

#include "pattern_collection_information.h"
#include "types.h"

#include "../task_proxy.h"

#include <memory>

namespace pdbs {
class IncrementalPDBs {
protected:
    TaskProxy task_proxy;

    std::shared_ptr<PatternCollection> patterns;
    std::shared_ptr<PDBCollection> pattern_databases;

    // The sum of all abstract state sizes of all pdbs in the collection.
    int size;

    void add_pdb_for_pattern(const Pattern &pattern);
public:
    IncrementalPDBs(const TaskProxy &task_proxy,
                    const PatternCollection &intitial_patterns);
    virtual ~IncrementalPDBs() = default;

    virtual void add_pdb(const std::shared_ptr<PatternDatabase> &pdb) = 0;
    virtual int get_value(const State &state) const = 0;
    virtual bool is_dead_end(const State &state) const = 0;
    virtual PatternCollectionInformation get_pattern_collection_information() const = 0;

    std::shared_ptr<PDBCollection> get_pattern_databases() const {
        return pattern_databases;
    }

    int get_size() const {
        return size;
    }
};
}

#endif

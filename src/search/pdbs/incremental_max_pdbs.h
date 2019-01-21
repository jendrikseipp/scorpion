#ifndef PDBS_INCREMENTAL_MAX_PDBS_H
#define PDBS_INCREMENTAL_MAX_PDBS_H

#include "incremental_pdbs.h"
#include "types.h"

#include <memory>

namespace pdbs {
class IncrementalMaxPDBs : public IncrementalPDBs {
public:
    explicit IncrementalMaxPDBs(const TaskProxy &task_proxy);

    virtual void add_pdb(const std::shared_ptr<PatternDatabase> &pdb) override;
    virtual int get_value(const State &state) const override;
    virtual bool is_dead_end(const State &state) const override;
    virtual PatternCollectionInformation get_pattern_collection_information() const override;
};
}

#endif

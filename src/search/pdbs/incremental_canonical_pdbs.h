#ifndef PDBS_INCREMENTAL_CANONICAL_PDBS_H
#define PDBS_INCREMENTAL_CANONICAL_PDBS_H

#include "incremental_pdbs.h"
#include "max_additive_pdb_sets.h"
#include "types.h"

namespace pdbs {
class IncrementalCanonicalPDBs : public IncrementalPDBs {
    std::shared_ptr<MaxAdditivePDBSubsets> max_additive_subsets;

    // A pair of variables is additive if no operator has an effect on both.
    VariableAdditivity are_additive;

    // Adds a PDB for pattern but does not recompute max_additive_subsets.
    void add_pdb_for_pattern(const Pattern &pattern);

    void recompute_max_additive_subsets();
public:
    IncrementalCanonicalPDBs(const TaskProxy &task_proxy,
                             const PatternCollection &intitial_patterns);

    // Adds a new PDB to the collection and recomputes max_additive_subsets.
    virtual void add_pdb(const std::shared_ptr<PatternDatabase> &pdb) override;

    /* Returns a set of subsets that would be additive to the new pattern.
       Detailed documentation in max_additive_pdb_sets.h */
    MaxAdditivePDBSubsets get_max_additive_subsets(const Pattern &new_pattern);

    virtual int get_value(const State &state) const override;

    /*
      The following method offers a quick dead-end check for the sampling
      procedure of iPDB-hillclimbing. This exists because we can much more
      efficiently test if the canonical heuristic is infinite than
      computing the exact heuristic value.
    */
    virtual bool is_dead_end(const State &state) const override;

    virtual PatternCollectionInformation get_pattern_collection_information() const override;
};
}

#endif

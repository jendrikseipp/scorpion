#ifndef SEARCH_ENGINES_EXHAUSTIVE_SEARCH_H
#define SEARCH_ENGINES_EXHAUSTIVE_SEARCH_H

#include <fstream>

#include "../search_engine.h"

namespace options {
class Options;
}

namespace exhaustive_search {
class ExhaustiveSearch : public SearchEngine {
    int current_state_id;
    std::vector<std::vector<int>> fact_mapping;

    void dump_state(const State &state);

    bool dump_atoms_to_file;
    bool dump_states_to_file;
    bool dump_transitions_to_file;

    std::ofstream states_file;
    std::ofstream transitions_file;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit ExhaustiveSearch(const options::Options &opts);

    virtual void print_statistics() const override;
};
}

#endif

#ifndef AXIOMS_AXIOM_RULES_H
#define AXIOMS_AXIOM_RULES_H

#include "../pddl/action.h"
#include "../pddl/axiom.h"
#include "../pddl/condition.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace translate::axioms {
struct AxiomLayering {
    std::vector<std::shared_ptr<pddl::PropositionalAxiom>> axioms;
    // Atom hash key (predicate + args concatenated) -> layer index.
    std::unordered_map<std::string, int> axiom_layers;
};

AxiomLayering handle_axioms(
    const std::vector<std::shared_ptr<pddl::PropositionalAction>> &operators,
    const std::vector<std::shared_ptr<pddl::PropositionalAxiom>> &axioms,
    const std::vector<pddl::ConditionPtr> &goals,
    const std::string &layer_strategy);

// Build a canonical string key for a (positive) atom used in layer maps.
std::string atom_key(const pddl::Atom &atom);
}

#endif

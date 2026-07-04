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
    // Layer index for each derived variable, identified by its effect atom (so
    // the caller can map it to a FactId in its own fluent-fact table).
    struct LayeredEffect {
        std::shared_ptr<const pddl::Atom> effect;
        int layer;
    };
    std::vector<LayeredEffect> axiom_layers;
};

AxiomLayering handle_axioms(
    const std::vector<pddl::PropositionalAction> &operators,
    const std::vector<std::shared_ptr<pddl::PropositionalAxiom>> &axioms,
    const std::vector<pddl::ConditionPtr> &goals,
    // FactId -> Atom, to recover atom keys from action GroundLiterals.
    const std::vector<std::shared_ptr<const pddl::Atom>> &fact_by_id,
    const std::string &layer_strategy);

}

#endif

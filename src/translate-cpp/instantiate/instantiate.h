#ifndef INSTANTIATE_INSTANTIATE_H
#define INSTANTIATE_INSTANTIATE_H

#include "../grounding/program.h"
#include "../pddl/task.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace translate::instantiate {
struct Result {
    bool relaxed_reachable = false;
    pddl::AtomSet fluent_facts;
    // Dense FactId for each fluent fact, and the reverse map. Action literals
    // are stored as GroundLiteral (FactId + sign); these let the translator map
    // fact-group atoms to FactIds (to build the FactId -> (var,val) table) and
    // rebuild the few axiom/goal literals as atoms.
    pddl::FactMap fluent_fact_ids;
    std::vector<std::shared_ptr<const pddl::Atom>> fact_by_id;
    // Held by value: no one shares ownership, and on the hard instances this
    // is millions of actions -- a shared_ptr each would add a control block and
    // a pointer indirection in the two hottest consumers (axiom analysis and
    // the translate loop).
    std::vector<pddl::PropositionalAction> instantiated_actions;
    /*
      The instantiated goal as a list of literals, or std::nullopt if
      the goal is impossible due to static facts.
    */
    std::optional<std::vector<pddl::ConditionPtr>> instantiated_goal;
    std::vector<std::shared_ptr<pddl::PropositionalAxiom>> instantiated_axioms;
    // For each action (by index in task.actions), the list of argument tuples
    // that gave a reachable grounding (mirrors Python's
    // reachable_action_parameters dict). Arguments are interned object ids
    // (grounding symbol table) rather than name strings: this table is only
    // consumed by the invariant finder's per-parameter "ever equal?" test,
    // which needs equality, and ids are far smaller to hold through the
    // memory-heavy translation phase.
    std::vector<std::vector<std::vector<int>>> reachable_action_parameters;
};

/*
  Walk the Datalog model and produce instantiated actions, axioms, and
  goal. The task must be normalized.
*/
Result instantiate(
    const pddl::Task &task, const std::vector<grounding::Atom> &model,
    const grounding::PredicateRoles &roles);
}

#endif

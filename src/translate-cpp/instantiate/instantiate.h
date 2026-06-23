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
    std::vector<std::shared_ptr<pddl::PropositionalAction>>
        instantiated_actions;
    /*
      The instantiated goal as a list of literals, or std::nullopt if
      the goal is impossible due to static facts.
    */
    std::optional<std::vector<pddl::ConditionPtr>> instantiated_goal;
    std::vector<std::shared_ptr<pddl::PropositionalAxiom>>
        instantiated_axioms;
    // For each action (by index in task.actions), the list of argument
    // tuples that gave a reachable grounding (mirrors Python's
    // reachable_action_parameters dict).
    std::vector<std::vector<std::vector<std::string>>>
        reachable_action_parameters;
};

/*
  Walk the Datalog model and produce instantiated actions, axioms, and
  goal. The task must be normalized.
*/
Result instantiate(const pddl::Task &task,
                   const std::vector<grounding::Atom> &model);
}

#endif

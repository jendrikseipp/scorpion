#ifndef FACT_GROUPS_FACT_GROUPS_H
#define FACT_GROUPS_FACT_GROUPS_H

#include "../pddl/condition.h"
#include "../pddl/task.h"

#include <string>
#include <vector>

namespace translate::fact_groups {
struct ComputedGroups {
    /*
      Selected mutex groups plus singleton groups for uncovered facts.
      These are the SAS+ variables.
    */
    std::vector<std::vector<pddl::ConditionPtr>> groups;
    /*
      All discovered mutex groups (>=2) plus singleton groups for
      uncovered facts. Used by the SAS+ writer's "begin_mutex_group"
      section.
    */
    std::vector<std::vector<pddl::ConditionPtr>> mutex_groups;
    /*
      Per-group human-readable atom labels plus one trailing label for
      the "other value" (the negation for singletons, or
      "<none of those>" for multi-atom groups).
    */
    std::vector<std::vector<std::string>> translation_key;
};

ComputedGroups compute_groups(
    const pddl::Task &task, const pddl::AtomSet &atoms,
    const std::vector<std::vector<std::vector<std::string>>>
        *reachable_action_parameters,
    const pddl::AtomSet &negative_in_goal);
}

#endif

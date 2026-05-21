#include "task.h"

#include <algorithm>
#include <stdexcept>

namespace translate::pddl {
Requirements::Requirements(std::vector<std::string> reqs)
    : requirements(std::move(reqs)) {
    for (const auto &r : requirements) {
        auto it = std::find(REQUIREMENT_LABELS.begin(),
                            REQUIREMENT_LABELS.end(), r);
        if (it == REQUIREMENT_LABELS.end())
            throw std::runtime_error("Invalid requirement: " + r);
    }
}

std::ostream &operator<<(std::ostream &os, const Requirements &r) {
    for (std::size_t i = 0; i < r.requirements.size(); ++i) {
        if (i) os << ", ";
        os << r.requirements[i];
    }
    return os;
}

Axiom *Task::add_axiom(std::vector<TypedObject> parameters, ConditionPtr cond) {
    std::string name = "new-axiom@" + std::to_string(axiom_counter++);
    predicates.emplace_back(name, parameters);
    int arity = static_cast<int>(parameters.size());
    axioms.emplace_back(name, std::move(parameters), arity, std::move(cond));
    return &axioms.back();
}

void Task::dump(std::ostream &os) const {
    os << "Problem " << domain_name << ": " << task_name
       << " [" << requirements << "]\n"
       << "Types:\n";
    for (const auto &t : types)
        os << "  " << t << "\n";
    os << "Objects:\n";
    for (const auto &o : objects)
        os << "  " << o << "\n";
    os << "Predicates:\n";
    for (const auto &p : predicates)
        os << "  " << p << "\n";
    os << "Functions:\n";
    for (const auto &f : functions)
        os << "  " << f << "\n";
    os << "Init:\n";
    for (const auto &i : init) {
        os << "  ";
        std::visit([&os](const auto &v) {
            if (v) v->dump(os, 0);
        }, i);
    }
    os << "Goal:\n";
    if (goal) goal->dump(os, 1);
    os << "Actions:\n";
    for (const auto &a : actions)
        a.dump(os);
    if (!axioms.empty()) {
        os << "Axioms:\n";
        for (const auto &a : axioms)
            a.dump(os);
    }
}
}

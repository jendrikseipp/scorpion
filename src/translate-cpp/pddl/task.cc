#include "task.h"

#include <algorithm>
#include <stdexcept>

using namespace std;
namespace translate::pddl {
Requirements::Requirements(vector<string> reqs)
    : requirements(move(reqs)) {
    for (const auto &r : requirements) {
        auto it = ranges::find(REQUIREMENT_LABELS, r);
        if (it == REQUIREMENT_LABELS.end())
            throw runtime_error("Invalid requirement: " + r);
    }
}

ostream &operator<<(ostream &os, const Requirements &r) {
    for (size_t i = 0; i < r.requirements.size(); ++i) {
        if (i) os << ", ";
        os << r.requirements[i];
    }
    return os;
}

Axiom *Task::add_axiom(vector<TypedObject> parameters, ConditionPtr cond) {
    string name = "new-axiom@" + to_string(axiom_counter++);
    predicates.emplace_back(name, parameters);
    int arity = static_cast<int>(parameters.size());
    axioms.emplace_back(name, move(parameters), arity, move(cond));
    return &axioms.back();
}

void Task::dump(ostream &os) const {
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
        visit([&os](const auto &v) {
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

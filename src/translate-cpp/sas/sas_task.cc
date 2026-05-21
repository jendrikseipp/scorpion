#include "sas_task.h"

#include <algorithm>
#include <set>

namespace translate::sas {
void SASVariables::output(std::ostream &os) const {
    os << ranges.size() << "\n";
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        os << "begin_variable\n"
           << "var" << i << "\n"
           << axiom_layers[i] << "\n"
           << ranges[i] << "\n";
        for (const auto &v : value_names[i]) os << v << "\n";
        os << "end_variable\n";
    }
}

int SASVariables::get_encoding_size() const {
    int size = static_cast<int>(ranges.size());
    for (int r : ranges) size += r;
    return size;
}

SASMutexGroup::SASMutexGroup(std::vector<VarVal> f) : facts(std::move(f)) {
    std::sort(facts.begin(), facts.end());
}

void SASMutexGroup::output(std::ostream &os) const {
    os << "begin_mutex_group\n" << facts.size() << "\n";
    for (const auto &[v, val] : facts) os << v << " " << val << "\n";
    os << "end_mutex_group\n";
}

void SASInit::output(std::ostream &os) const {
    os << "begin_state\n";
    for (int v : values) os << v << "\n";
    os << "end_state\n";
}

SASGoal::SASGoal(std::vector<VarVal> p) : pairs(std::move(p)) {
    std::sort(pairs.begin(), pairs.end());
}

void SASGoal::output(std::ostream &os) const {
    os << "begin_goal\n" << pairs.size() << "\n";
    for (const auto &[v, val] : pairs) os << v << " " << val << "\n";
    os << "end_goal\n";
}

void SASOperator::output(std::ostream &os) const {
    // Strip outer parens for name as Python does.
    std::string clean = name;
    if (clean.size() >= 2 && clean.front() == '(' && clean.back() == ')')
        clean = clean.substr(1, clean.size() - 2);

    // Sort prevail and pre_post for determinism.
    auto prevail_sorted = prevail;
    std::sort(prevail_sorted.begin(), prevail_sorted.end());

    auto pp_sorted = pre_post;
    for (auto &[v, pre, post, cond] : pp_sorted)
        std::sort(cond.begin(), cond.end());
    std::sort(pp_sorted.begin(), pp_sorted.end());
    pp_sorted.erase(std::unique(pp_sorted.begin(), pp_sorted.end()),
                    pp_sorted.end());

    os << "begin_operator\n" << clean << "\n";
    os << prevail_sorted.size() << "\n";
    for (const auto &[v, val] : prevail_sorted) os << v << " " << val << "\n";
    os << pp_sorted.size() << "\n";
    for (const auto &[v, pre, post, cond] : pp_sorted) {
        os << cond.size();
        for (const auto &[cv, cval] : cond) os << " " << cv << " " << cval;
        os << " " << v << " " << pre << " " << post << "\n";
    }
    os << cost << "\n";
    os << "end_operator\n";
}

void SASAxiom::output(std::ostream &os) const {
    auto cond = condition;
    std::sort(cond.begin(), cond.end());
    os << "begin_rule\n" << cond.size() << "\n";
    for (const auto &[v, val] : cond) os << v << " " << val << "\n";
    os << effect.first << " " << (1 - effect.second) << " " << effect.second
       << "\n";
    os << "end_rule\n";
}

void SASTask::output(std::ostream &os) const {
    os << "begin_version\n" << SAS_FILE_VERSION << "\nend_version\n";
    os << "begin_metric\n" << (metric ? 1 : 0) << "\nend_metric\n";
    variables.output(os);
    os << mutexes.size() << "\n";
    for (const auto &m : mutexes) m.output(os);
    init.output(os);
    goal.output(os);
    // Sort operators by (name, prevail, pre_post).
    auto ops = operators;
    std::sort(ops.begin(), ops.end(),
              [](const SASOperator &a, const SASOperator &b) {
                  if (a.name != b.name) return a.name < b.name;
                  if (a.prevail != b.prevail) return a.prevail < b.prevail;
                  return a.pre_post < b.pre_post;
              });
    os << ops.size() << "\n";
    for (const auto &op : ops) op.output(os);
    auto axs = axioms;
    std::sort(axs.begin(), axs.end(),
              [](const SASAxiom &a, const SASAxiom &b) {
                  if (a.condition != b.condition) return a.condition < b.condition;
                  return a.effect < b.effect;
              });
    os << axs.size() << "\n";
    for (const auto &a : axs) a.output(os);
}
}

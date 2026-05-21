#include "sas_task.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <string>

namespace translate::sas {
namespace {
/*
  Fast writer that appends to a std::string buffer using locale-free
  std::to_chars for integers. Used by SASTask::output to avoid the
  per-`<<` locale handling that dominated the output phase: dumping a
  10 MB output.sas with operator<< was ~150 ms; the fast writer reduces
  this to a few tens of ms.
*/
class FastWriter {
public:
    std::string &buf;
    explicit FastWriter(std::string &b) : buf(b) {}
    void put(char c) { buf.push_back(c); }
    void put(std::string_view s) { buf.append(s); }
    void put(const std::string &s) { buf.append(s); }
    void put(int n) {
        std::array<char, 16> tmp;
        auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), n);
        buf.append(tmp.data(), r.ptr);
    }
    void put(std::size_t n) {
        std::array<char, 24> tmp;
        auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), n);
        buf.append(tmp.data(), r.ptr);
    }
    void nl() { buf.push_back('\n'); }
};

void write_variables(FastWriter &w, const SASVariables &vars) {
    w.put(vars.ranges.size()); w.nl();
    for (std::size_t i = 0; i < vars.ranges.size(); ++i) {
        w.put(std::string_view("begin_variable")); w.nl();
        w.put(std::string_view("var")); w.put(i); w.nl();
        w.put(vars.axiom_layers[i]); w.nl();
        w.put(vars.ranges[i]); w.nl();
        for (const auto &v : vars.value_names[i]) { w.put(v); w.nl(); }
        w.put(std::string_view("end_variable")); w.nl();
    }
}

void write_mutex(FastWriter &w, const SASMutexGroup &m) {
    w.put(std::string_view("begin_mutex_group")); w.nl();
    w.put(m.facts.size()); w.nl();
    for (const auto &[v, val] : m.facts) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(std::string_view("end_mutex_group")); w.nl();
}

void write_init(FastWriter &w, const SASInit &init) {
    w.put(std::string_view("begin_state")); w.nl();
    for (int v : init.values) { w.put(v); w.nl(); }
    w.put(std::string_view("end_state")); w.nl();
}

void write_goal(FastWriter &w, const SASGoal &goal) {
    w.put(std::string_view("begin_goal")); w.nl();
    w.put(goal.pairs.size()); w.nl();
    for (const auto &[v, val] : goal.pairs) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(std::string_view("end_goal")); w.nl();
}

void write_operator(FastWriter &w, const SASOperator &op) {
    // Strip outer parens for name as Python does.
    std::string_view clean = op.name;
    if (clean.size() >= 2 && clean.front() == '(' && clean.back() == ')')
        clean = clean.substr(1, clean.size() - 2);

    // Sort prevail and pre_post for determinism.
    auto prevail_sorted = op.prevail;
    std::sort(prevail_sorted.begin(), prevail_sorted.end());

    auto pp_sorted = op.pre_post;
    for (auto &[v, pre, post, cond] : pp_sorted)
        std::sort(cond.begin(), cond.end());
    std::sort(pp_sorted.begin(), pp_sorted.end());
    pp_sorted.erase(std::unique(pp_sorted.begin(), pp_sorted.end()),
                    pp_sorted.end());

    w.put(std::string_view("begin_operator")); w.nl();
    w.put(clean); w.nl();
    w.put(prevail_sorted.size()); w.nl();
    for (const auto &[v, val] : prevail_sorted) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(pp_sorted.size()); w.nl();
    for (const auto &[v, pre, post, cond] : pp_sorted) {
        w.put(cond.size());
        for (const auto &[cv, cval] : cond) {
            w.put(' '); w.put(cv); w.put(' '); w.put(cval);
        }
        w.put(' '); w.put(v);
        w.put(' '); w.put(pre);
        w.put(' '); w.put(post); w.nl();
    }
    w.put(op.cost); w.nl();
    w.put(std::string_view("end_operator")); w.nl();
}

void write_axiom(FastWriter &w, const SASAxiom &ax) {
    auto cond = ax.condition;
    std::sort(cond.begin(), cond.end());
    w.put(std::string_view("begin_rule")); w.nl();
    w.put(cond.size()); w.nl();
    for (const auto &[v, val] : cond) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(ax.effect.first); w.put(' '); w.put(1 - ax.effect.second);
    w.put(' '); w.put(ax.effect.second); w.nl();
    w.put(std::string_view("end_rule")); w.nl();
}
}

// Legacy ostream-based versions retained for callers that pass an
// ostream (e.g. dump for debugging). The fast path is via SASTask::output
// which routes through FastWriter + a single ofstream::write.
void SASVariables::output(std::ostream &os) const {
    std::string buf; FastWriter w(buf); write_variables(w, *this);
    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
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
    std::string buf; FastWriter w(buf); write_mutex(w, *this);
    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}
void SASInit::output(std::ostream &os) const {
    std::string buf; FastWriter w(buf); write_init(w, *this);
    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}
SASGoal::SASGoal(std::vector<VarVal> p) : pairs(std::move(p)) {
    std::sort(pairs.begin(), pairs.end());
}
void SASGoal::output(std::ostream &os) const {
    std::string buf; FastWriter w(buf); write_goal(w, *this);
    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}
void SASOperator::output(std::ostream &os) const {
    std::string buf; FastWriter w(buf); write_operator(w, *this);
    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}
void SASAxiom::output(std::ostream &os) const {
    std::string buf; FastWriter w(buf); write_axiom(w, *this);
    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}

void SASTask::output(std::ostream &os) const {
    // Stream everything into a single std::string buffer using locale-free
    // formatters, then write it out with one os.write() call.
    std::string buf;
    buf.reserve(1u << 20); // start at 1 MB
    FastWriter w(buf);

    w.put(std::string_view("begin_version")); w.nl();
    w.put(SAS_FILE_VERSION); w.nl();
    w.put(std::string_view("end_version")); w.nl();
    w.put(std::string_view("begin_metric")); w.nl();
    w.put(metric ? 1 : 0); w.nl();
    w.put(std::string_view("end_metric")); w.nl();

    write_variables(w, variables);
    w.put(mutexes.size()); w.nl();
    for (const auto &m : mutexes) write_mutex(w, m);
    write_init(w, init);
    write_goal(w, goal);

    auto ops = operators;
    std::sort(ops.begin(), ops.end(),
              [](const SASOperator &a, const SASOperator &b) {
                  if (a.name != b.name) return a.name < b.name;
                  if (a.prevail != b.prevail) return a.prevail < b.prevail;
                  return a.pre_post < b.pre_post;
              });
    w.put(ops.size()); w.nl();
    for (const auto &op : ops) write_operator(w, op);

    auto axs = axioms;
    std::sort(axs.begin(), axs.end(),
              [](const SASAxiom &a, const SASAxiom &b) {
                  if (a.condition != b.condition) return a.condition < b.condition;
                  return a.effect < b.effect;
              });
    w.put(axs.size()); w.nl();
    for (const auto &a : axs) write_axiom(w, a);

    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}
}

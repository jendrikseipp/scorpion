#include "sas_task.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <string>

using namespace std;
namespace translate::sas {
namespace {
/*
  Fast writer that appends to a string buffer using locale-free
  to_chars for integers. Used by SASTask::output to avoid the
  per-`<<` locale handling that dominated the output phase: dumping a
  10 MB output.sas with operator<< was ~150 ms; the fast writer reduces
  this to a few tens of ms.
*/
class FastWriter {
public:
    string &buf;
    explicit FastWriter(string &b) : buf(b) {}
    void put(char c) { buf.push_back(c); }
    void put(string_view s) { buf.append(s); }
    void put(const string &s) { buf.append(s); }
    void put(int n) {
        array<char, 16> tmp;
        auto r = to_chars(tmp.data(), tmp.data() + tmp.size(), n);
        buf.append(tmp.data(), r.ptr);
    }
    void put(size_t n) {
        array<char, 24> tmp;
        auto r = to_chars(tmp.data(), tmp.data() + tmp.size(), n);
        buf.append(tmp.data(), r.ptr);
    }
    void nl() { buf.push_back('\n'); }
};

void write_variables(FastWriter &w, const SASVariables &vars) {
    w.put(vars.ranges.size()); w.nl();
    for (size_t i = 0; i < vars.ranges.size(); ++i) {
        w.put(string_view("begin_variable")); w.nl();
        w.put(string_view("var")); w.put(i); w.nl();
        w.put(vars.axiom_layers[i]); w.nl();
        w.put(vars.ranges[i]); w.nl();
        for (const auto &v : vars.value_names[i]) { w.put(v); w.nl(); }
        w.put(string_view("end_variable")); w.nl();
    }
}

void write_mutex(FastWriter &w, const SASMutexGroup &m) {
    w.put(string_view("begin_mutex_group")); w.nl();
    w.put(m.facts.size()); w.nl();
    for (const auto &[v, val] : m.facts) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(string_view("end_mutex_group")); w.nl();
}

void write_init(FastWriter &w, const SASInit &init) {
    w.put(string_view("begin_state")); w.nl();
    for (int v : init.values) { w.put(v); w.nl(); }
    w.put(string_view("end_state")); w.nl();
}

void write_goal(FastWriter &w, const SASGoal &goal) {
    w.put(string_view("begin_goal")); w.nl();
    w.put(goal.pairs.size()); w.nl();
    for (const auto &[v, val] : goal.pairs) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(string_view("end_goal")); w.nl();
}

void write_operator(FastWriter &w, const SASOperator &op) {
    // Strip outer parens for name as Python does.
    string_view clean = op.name;
    if (clean.size() >= 2 && clean.front() == '(' && clean.back() == ')')
        clean = clean.substr(1, clean.size() - 2);

    /*
      Emit pre_post and prevail in whatever order they currently have.
      Canonicalization (sort + uniq) is now done once at construction
      in pipeline::build_sas_operator and simplify::translate_operator,
      so that variable_order's remap reshuffles the canonical order
      without re-sorting -- matching the Python translator's
      SASOperator._canonical_pre_post-then-remap flow. Re-sorting here
      would put pre_post in ascending post-remap-var order, which is
      a different (but valid) order from Python's.
    */
    w.put(string_view("begin_operator")); w.nl();
    w.put(clean); w.nl();
    w.put(op.prevail.size()); w.nl();
    for (const auto &[v, val] : op.prevail) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(op.pre_post.size()); w.nl();
    for (const auto &[v, pre, post, cond] : op.pre_post) {
        w.put(cond.size());
        for (const auto &[cv, cval] : cond) {
            w.put(' '); w.put(cv); w.put(' '); w.put(cval);
        }
        w.put(' '); w.put(v);
        w.put(' '); w.put(pre);
        w.put(' '); w.put(post); w.nl();
    }
    w.put(op.cost); w.nl();
    w.put(string_view("end_operator")); w.nl();
}

void write_axiom(FastWriter &w, const SASAxiom &ax) {
    auto cond = ax.condition;
    ranges::sort(cond);
    w.put(string_view("begin_rule")); w.nl();
    w.put(cond.size()); w.nl();
    for (const auto &[v, val] : cond) {
        w.put(v); w.put(' '); w.put(val); w.nl();
    }
    w.put(ax.effect.first); w.put(' '); w.put(1 - ax.effect.second);
    w.put(' '); w.put(ax.effect.second); w.nl();
    w.put(string_view("end_rule")); w.nl();
}
}

// Legacy ostream-based versions retained for callers that pass an
// ostream (e.g. dump for debugging). The fast path is via SASTask::output
// which routes through FastWriter + a single ofstream::write.
void SASVariables::output(ostream &os) const {
    string buf; FastWriter w(buf); write_variables(w, *this);
    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}
int SASVariables::get_encoding_size() const {
    int size = static_cast<int>(ranges.size());
    for (int r : ranges) size += r;
    return size;
}
SASMutexGroup::SASMutexGroup(vector<VarVal> f) : facts(move(f)) {
    ranges::sort(facts);
}
void SASMutexGroup::output(ostream &os) const {
    string buf; FastWriter w(buf); write_mutex(w, *this);
    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}
void SASInit::output(ostream &os) const {
    string buf; FastWriter w(buf); write_init(w, *this);
    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}
SASGoal::SASGoal(vector<VarVal> p) : pairs(move(p)) {
    ranges::sort(pairs);
}
void SASGoal::output(ostream &os) const {
    string buf; FastWriter w(buf); write_goal(w, *this);
    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}
void SASOperator::output(ostream &os) const {
    string buf; FastWriter w(buf); write_operator(w, *this);
    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}
void SASAxiom::output(ostream &os) const {
    string buf; FastWriter w(buf); write_axiom(w, *this);
    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}

void SASTask::output(ostream &os) const {
    // Stream everything into a single string buffer using locale-free
    // formatters, then write it out with one os.write() call.
    string buf;
    buf.reserve(1u << 20); // start at 1 MB
    FastWriter w(buf);

    w.put(string_view("begin_version")); w.nl();
    w.put(SAS_FILE_VERSION); w.nl();
    w.put(string_view("end_version")); w.nl();
    w.put(string_view("begin_metric")); w.nl();
    w.put(metric ? 1 : 0); w.nl();
    w.put(string_view("end_metric")); w.nl();

    write_variables(w, variables);
    w.put(mutexes.size()); w.nl();
    for (const auto &m : mutexes) write_mutex(w, m);
    write_init(w, init);
    write_goal(w, goal);

    // Emit operators and axioms in their current list order.
    // Canonical sorting is done once at SASTask construction in
    // pipeline::pddl_to_sas (before simplify/variable_order), matching
    // Python's SASTask.__init__. Re-sorting here would use post-remap
    // variable numbers and produce a different (but valid) order.
    w.put(operators.size()); w.nl();
    for (const auto &op : operators) write_operator(w, op);

    auto axs = axioms;
    // Canonicalize within-rule condition order (by final variable number)
    // before ordering the rules, so both the rule sort key and the emitted
    // conditions use the same order -- matching the Python translator, whose
    // final axiom sort also operates on per-rule-sorted conditions.
    for (auto &a : axs) ranges::sort(a.condition);
    ranges::sort(axs,
              [](const SASAxiom &a, const SASAxiom &b) {
                  if (a.condition != b.condition) return a.condition < b.condition;
                  return a.effect < b.effect;
              });
    w.put(axs.size()); w.nl();
    for (const auto &a : axs) write_axiom(w, a);

    os.write(buf.data(), static_cast<streamsize>(buf.size()));
}
}

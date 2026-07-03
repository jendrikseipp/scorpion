#ifndef SAS_SAS_TASK_H
#define SAS_SAS_TASK_H

#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace translate::sas {
inline constexpr int SAS_FILE_VERSION = 3;

using VarVal = std::pair<int, int>;

struct SASVariables {
    std::vector<int> ranges;
    std::vector<int> axiom_layers;
    std::vector<std::vector<std::string>> value_names;
    void output(std::ostream &os) const;
    int get_encoding_size() const;
};

struct SASMutexGroup {
    std::vector<VarVal> facts;
    SASMutexGroup() = default;
    explicit SASMutexGroup(std::vector<VarVal> facts);
    void output(std::ostream &os) const;
};

struct SASInit {
    std::vector<int> values;
    void output(std::ostream &os) const;
};

struct SASGoal {
    std::vector<VarVal> pairs;
    SASGoal() = default;
    explicit SASGoal(std::vector<VarVal> pairs);
    void output(std::ostream &os) const;
};

/*
  One conditional effect: variable `var` changes from `pre` (-1 if
  unconditioned) to `post`, guarded by the (var, val) pairs in `conditions`.
  Member order matches the old tuple<int, int, int, vector<VarVal>>, so the
  defaulted comparison keeps the exact (var, pre, post, conditions) ordering
  the canonicalization/dedup/output paths rely on.
*/
struct PrePost {
    int var;
    int pre;
    int post;
    std::vector<VarVal> conditions;
    auto operator<=>(const PrePost &) const = default;
};

struct SASOperator {
    std::string name;
    std::vector<VarVal> prevail;
    std::vector<PrePost> pre_post;
    int cost = 1;
    void output(std::ostream &os) const;
};

struct SASAxiom {
    std::vector<VarVal> condition;
    VarVal effect;
    void output(std::ostream &os) const;
};

struct SASTask {
    SASVariables variables;
    std::vector<SASMutexGroup> mutexes;
    SASInit init;
    SASGoal goal;
    std::vector<SASOperator> operators;
    std::vector<SASAxiom> axioms;
    bool metric = false;

    // Remove operators with identical prevail, pre_post and cost, keeping the
    // first occurrence. Returns the number removed. Mirrors the Python
    // translator's SASTask.remove_duplicate_operators.
    int remove_duplicate_operators();

    void output(std::ostream &os) const;
};
}

#endif

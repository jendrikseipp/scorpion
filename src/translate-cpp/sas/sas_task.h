#ifndef SAS_SAS_TASK_H
#define SAS_SAS_TASK_H

#include <ostream>
#include <string>
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

struct SASOperator {
    std::string name;
    std::vector<VarVal> prevail;
    /*
      Each entry: (var, pre, post, cond) where cond is a list of (var, val)
      effect conditions.
    */
    std::vector<std::tuple<int, int, int, std::vector<VarVal>>> pre_post;
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

    void output(std::ostream &os) const;
};
}

#endif

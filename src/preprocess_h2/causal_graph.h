#ifndef CAUSAL_GRAPH_H
#define CAUSAL_GRAPH_H

#include <iosfwd>
#include <map>
#include <unordered_set>
#include <vector>

class Operator;
class Axiom;
class Variable;

class CausalGraph {
    const std::vector<Variable *> &variables;
    const std::vector<Operator> &operators;
    const std::vector<Axiom> &axioms;
    const std::vector<std::pair<Variable *, int>> &goals;

    typedef std::map<Variable *, int> WeightedSuccessors;
    typedef std::map<Variable *, WeightedSuccessors> WeightedGraph;
    WeightedGraph weighted_graph;
    typedef std::map<Variable *, int> Predecessors;
    typedef std::map<Variable *, Predecessors> PredecessorGraph;
    // predecessor_graph is weighted_graph with edges turned around.
    PredecessorGraph predecessor_graph;

    typedef std::vector<std::vector<Variable *>> Partition;
    typedef std::vector<Variable *> Ordering;
    Ordering ordering;
    bool acyclic;

    void weigh_graph_from_ops(
        const std::vector<Variable *> &variables,
        const std::vector<Operator> &operators,
        const std::vector<std::pair<Variable *, int>> &goals);
    void weigh_graph_from_axioms(
        const std::vector<Variable *> &variables,
        const std::vector<Axiom> &axioms,
        const std::vector<std::pair<Variable *, int>> &goals);
    void get_strongly_connected_components(
        const std::vector<Variable *> &variables, Partition &sccs);
    void calculate_topological_pseudo_sort(const Partition &sccs);
    void calculate_important_vars();
    void dfs(Variable *from, std::unordered_set<Variable *> &necessary_vars);
public:
    CausalGraph(
        const std::vector<Variable *> &variables,
        const std::vector<Operator> &operators,
        const std::vector<Axiom> &axioms,
        const std::vector<std::pair<Variable *, int>> &the_goals);
    ~CausalGraph() = default;
    const std::vector<Variable *> &get_variable_ordering() const;
    bool is_acyclic() const;
    void dump() const;
    void generate_cpp_input(
        std::ofstream &outfile,
        const std::vector<Variable *> &ordered_vars) const;
    void update();
};

extern bool g_do_not_prune_variables;

#endif

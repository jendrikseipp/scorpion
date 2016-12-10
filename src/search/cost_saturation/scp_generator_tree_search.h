#ifndef COST_SATURATION_SCP_GENERATOR_TREE_SEARCH_H
#define COST_SATURATION_SCP_GENERATOR_TREE_SEARCH_H

#include "cost_partitioning_generator.h"

#include <set>

namespace cost_saturation {
using AbstractionOrder = std::vector<int>;

class SearchNode {
public:
    SearchNode()
        : num_visits(0), solved(false) {}

    std::vector<std::unique_ptr<SearchNode>> children;
    int num_visits;
    bool solved;
};

class SCPGeneratorTreeSearch : public SCPGenerator {
public:
    explicit SCPGeneratorTreeSearch(const options::Options &opts);

    virtual CostPartitioning get_next_cost_partitioning(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) override;

    bool has_next_cost_partitioning() const;

protected:
    virtual void initialize(
        const TaskProxy &task_proxy,
        const std::vector<std::unique_ptr<Abstraction>> &abstractions,
        const std::vector<StateMap> &state_maps,
        const std::vector<int> &costs) override;

private:
    std::unique_ptr<SearchNode> root_node;
    size_t num_abstractions;

    std::set<int> vertices;
    std::vector<std::vector<int>> edges;

    AbstractionOrder current_order;
    std::set<int> current_vertices;
    std::vector<std::vector<int>> current_edges;

    void visit_node(SearchNode &node);
    void remove_vertex(int vertex);
    bool disjunct(std::vector<bool> &v1, std::vector<bool> &v2) const;
};
}

#endif

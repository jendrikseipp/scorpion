#ifndef SEARCH_ENGINES_NOVELTY_SEARCH_H
#define SEARCH_ENGINES_NOVELTY_SEARCH_H

#include "../open_list.h"
#include "../search_engine.h"

#include "../utils/timer.h"

#include <array>
#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

namespace options {
class Options;
}

namespace iterative_width_search {
class Conjunction;
struct ShortFact {
    uint16_t var;
    uint16_t value;

    static const int RANGE = static_cast<int>(std::numeric_limits<uint16_t>::max());

    ShortFact();
    ShortFact(int var, int value);

    bool operator==(const ShortFact &other) const {
        return var == other.var && value == other.value;
    }

    friend std::ostream &operator<<(std::ostream &os, const ShortFact &fact) {
        return os << fact.var << "=" << fact.value;
    }
};

static_assert(sizeof(ShortFact) == 4, "ShortFact has unexpected size");

using ConjunctionArray = std::array<ShortFact, 8>;
using ConjunctionList = std::vector<Conjunction>;
using ConjunctionSet = utils::HashSet<Conjunction>;
using Facts = std::vector<FactPair>;


class Conjunction {
    ConjunctionArray facts;
    uint8_t size_;
    uint8_t watched_index;
public:
    Conjunction();
    void push_back(FactPair fact);
    bool operator==(const Conjunction &other) const;
    FactPair operator[](int index) const;
    int get_watched_index() const;
    void set_watched_index(int index);
    int size() const;
    int capacity() const;
    ConjunctionArray::const_iterator begin() const;
    ConjunctionArray::const_iterator end() const;
    friend std::ostream &operator<<(std::ostream &os, const Conjunction &conjunction) {
        std::string sep = "";
        os << "(";
        for (auto &fact : conjunction) {
            os << sep << fact;
            sep = ", ";
        }
        return os << ")";
    }
};

static_assert(sizeof(Conjunction) == 34, "Conjunction has unexpected size");

void feed(utils::HashState &hash_state, const Conjunction &conjunction);


class NoveltySearch : public SearchEngine {
    const int width;
    const int condition_width;
    const bool debug;

    std::deque<StateID> open_list;

    std::vector<int> fact_id_offsets;
    std::vector<bool> seen_facts;
    std::vector<std::vector<bool>> seen_fact_pairs;

    std::vector<std::vector<Conjunction>> fact_watchers;

    utils::Timer compute_novelty_timer;

    int get_fact_id(FactPair fact) const {
        return fact_id_offsets[fact.var] + fact.value;
    }
    int get_fact_id(int var, int value) const {
        return fact_id_offsets[var] + value;
    }
    FactPair get_fact(int fact_id) const {
        for (int var = fact_id_offsets.size() - 1; var >= 0; --var) {
            if (fact_id_offsets[var] <= fact_id) {
                int value = fact_id - fact_id_offsets[var];
                return FactPair(var, value);
            }
        }
        ABORT("error");
    }
    bool visit_fact_pair(int fact_id1, int fact_id2);
    bool is_novel(const State &state);
    bool is_novel(OperatorID op_id, const State &state);

    void watch_all_conjunctions(int k);
    void watch_all_conjunctions();

    void add_subsets(
        const std::vector<FactPair> &facts, int k, ConjunctionSet &conjunctions) const;
    ConjunctionSet get_precondition_subsets() const;

    void dump_conjunction(
        const std::string &name, const Conjunction &conjunction) const;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit NoveltySearch(const options::Options &opts);
    virtual ~NoveltySearch() = default;

    virtual void print_statistics() const override;

    void dump_search_space() const;
};
}

#endif

#ifndef CEGAR_UTILS_H
#define CEGAR_UTILS_H

#include "types.h"

#include "../task_proxy.h"

#include "../utils/hash.h"

#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

class AbstractTask;

namespace additive_heuristic {
class AdditiveHeuristic;
}

namespace options {
class OptionParser;
}

namespace utils {
class RandomNumberGenerator;
}

namespace cegar {
class Abstraction;

extern int g_hacked_extra_memory_padding_mb;
extern OperatorOrdering g_hacked_operator_ordering;
extern bool g_hacked_sort_transitions;
extern TransitionRepresentation g_hacked_tsr;
extern std::shared_ptr<utils::RandomNumberGenerator> g_hacked_rng;

extern std::unique_ptr<additive_heuristic::AdditiveHeuristic>
create_additive_heuristic(const std::shared_ptr<AbstractTask> &task);

/*
  The set of relaxed-reachable facts is the possibly-before set of facts that
  can be reached in the delete-relaxation before 'fact' is reached the first
  time, plus 'fact' itself.
*/
extern utils::HashSet<FactProxy> get_relaxed_possible_before(
    const TaskProxy &task, const FactProxy &fact);

extern std::vector<int> get_domain_sizes(const TaskProxy &task);

extern void add_h_update_option(options::OptionParser &parser);
extern void add_operator_ordering_option(options::OptionParser &parser);
extern void add_transition_representation_option(options::OptionParser &parser);

template<typename T>
uint64_t estimate_memory_usage_in_bytes(const std::deque<T> &d) {
    uint64_t size = 0;
    size += sizeof(d);            // size of empty deque
    size += d.size() * sizeof(T); // size of actual entries
    return size;
}

// Adapted from utils::estimate_vector_bytes().
template<typename T>
uint64_t estimate_memory_usage_in_bytes(const std::vector<T> &vec) {
    uint64_t size = 0;
    size += 2 * sizeof(void *);         // overhead for dynamic memory management
    size += sizeof(vec);                // size of empty vector
    size += vec.capacity() * sizeof(T); // size of actual entries
    return size;
}

template<typename T>
uint64_t estimate_vector_of_vector_bytes(const std::vector<std::vector<T>> &vec) {
    uint64_t size = estimate_memory_usage_in_bytes(vec);
    for (auto &inner : vec) {
        size += estimate_memory_usage_in_bytes(inner);
    }
    size -= vec.capacity() * sizeof(T);  // Subtract doubly-counted bytes.
    return size;
}

extern void dump_dot_graph(const Abstraction &abstraction);
extern void write_dot_file_to_disk(const Abstraction &abstraction);
}

/*
  TODO: Our proxy classes are meant to be temporary objects and as such
  shouldn't be stored in containers. Once we find a way to avoid
  storing them in containers, we should remove this hashing function.
*/
namespace utils {
inline void feed(HashState &hash_state, const FactProxy &fact) {
    feed(hash_state, fact.get_pair());
}
}

#endif

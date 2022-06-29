#ifndef CEGAR_UTILS_H
#define CEGAR_UTILS_H

#include "types.h"

#include "../task_proxy.h"

#include "../utils/hash.h"

#include <memory>
#include <vector>

class AbstractTask;

namespace additive_heuristic {
class AdditiveHeuristic;
}

namespace options {
class OptionParser;
}

namespace cegar {
class Abstraction;

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

extern void add_common_cegar_options(options::OptionParser &parser);

extern std::string create_dot_graph(
    const TaskProxy &task_proxy, const Abstraction &abstraction);
extern void write_to_file(
    const std::string &file_name, const std::string &content);
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

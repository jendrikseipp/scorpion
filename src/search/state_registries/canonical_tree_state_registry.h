#ifndef CANONICAL_TREE_STATE_REGISTRY_H
#define CANONICAL_TREE_STATE_REGISTRY_H

#include "../abstract_task.h"
#include "../axioms.h"
#include "../state_id.h"
#include "../state_registry.h"

#include "../algorithms/int_packer.h"
#include "../algorithms/segmented_vector.h"
#include "../algorithms/subscriber.h"
#include "../utils/hash.h"
#include "../utils/storage_calc.h"

#include <valla/declarations.hpp>
#include <valla/static_tree_compression.hpp>

#include <parallel_hashmap/phmap.h>

#include <set>

#include "valla/bitset_pool.hpp"
#include "valla/fixed_hash_set.hpp"
#include "valla/canonical_fixed_tree_compression.hpp"


/*
  Overview of classes relevant to storing and working with registered states.

  State
    Objects of this class can represent registered or unregistered states.
    Registered states contain a pointer to the CanonicalTreeStateRegistry that created them
    and the ID they have there. Using this data, states can be used to index
    PerStateInformation objects.
    In addition, registered states have a pointer to the packed data of a state
    that is stored in their registry. Values of the state can be accessed
    through this pointer. For situations where a state's values have to be
    accessed a lot, the state's data can be unpacked. The unpacked data is
    stored in a vector<int> to which the state maintains a shared pointer.
    Unregistered states contain only this unpacked data. Compared to registered
    states, they are not guaranteed to be reachable and use no form of duplicate
    detection.
    Copying states is relatively cheap because the actual data does not have to
    be copied.

  StateID
    StateIDs identify states within a state registry.
    If the registry is known, the ID is sufficient to look up the state, which
    is why IDs are intended for long term storage (e.g. in open lists).
    Internally, a StateID is just an integer, so it is cheap to store and copy.

  -------------

  CanonicalTreeStateRegistry
    The CanonicalTreeStateRegistry allows to create states giving them an ID. IDs from
    different state registries must not be mixed.
    The CanonicalTreeStateRegistry also stores the actual state data in a memory friendly way.
    It uses the following class:

  SegmentedArrayVector<std::vector<int>>
    This class is used to store the actual (packed) state data for all states
    while avoiding dynamically allocating each state individually.
    The index within this vector corresponds to the ID of the state.

  PerStateInformation<T>
    Associates a value of type T with every state in a given CanonicalTreeStateRegistry.
    Can be thought of as a very compactly implemented map from State to T.
    References stay valid as long as the state registry exists. Memory usage is
    essentially the same as a vector<T> whose size is the number of states in
    the registry.


  ---------------
  Usage example 1
  ---------------
  Problem:
    A search node contains a state together with some information about how this
    state was reached and the status of the node. The state data is already
    stored and should not be duplicated. Open lists should in theory store search
    nodes but we want to keep the amount of data stored in the open list to a
    minimum.

  Solution:

    SearchNodeInfo
      Remaining part of a search node besides the state that needs to be stored.

    SearchNode
      A SearchNode combines a StateID, a reference to a SearchNodeInfo and
      OperatorCost. It is generated for easier access and not intended for long
      term storage. The state data is only stored once an can be accessed
      through the StateID.

    SearchSpace
      The SearchSpace uses PerStateInformation<SearchNodeInfo> to map StateIDs to
      SearchNodeInfos. The open lists only have to store StateIDs which can be
      used to look up a search node in the SearchSpace on demand.

  ---------------
  Usage example 2
  ---------------
  Problem:
    In the landmark heuristics each state should store which landmarks are
    already reached when this state is reached. This should only require
    additional memory when these heuristics are used.

  Solution:
    The heuristic object uses an attribute of type PerStateBitset to store for each
    state and each landmark whether it was reached in this state.
*/

namespace vs = valla;
namespace vsf = valla::canonical_fixed_tree;
namespace utils {
class LogProxy;
}


using IStateRegistry = StateRegistry;
class CanonicalTreeStateRegistry :
    public IStateRegistry {

    const int cap = entries_for_mb(500, sizeof(vs::IndexSlot));

    vs::FixedHashSetSlot root_table = vs::FixedHashSetSlot(cap,
                                                    vs::Hasher(),
                                                    vs::SlotEqual());
    vs::FixedHashSetSlot tree_table = vs::FixedHashSetSlot(cap,
                                                  vs::Hasher(),
                                                  vs::SlotEqual());
    vs::BitsetPool stored_traversals = vs::BitsetPool();
    vs::BitsetRepository traversal_repo = vs::BitsetRepository(stored_traversals);

    const vs::MergeSchedule merge_schedule_;
    size_t tree_size;


    const int_packer::IntPacker &state_packer;
    AxiomEvaluator &axiom_evaluator;
    const int num_variables;

    std::unique_ptr<State> cached_initial_state;
    StateID insert_id_or_pop_state();
    int get_bins_per_state() const;
public:
    explicit CanonicalTreeStateRegistry(const TaskProxy &task_proxy);

    std::vector<size_t> get_domain_sizes(const TaskProxy &task_proxy) const;

    StateID insert_state(const std::vector<vs::Index> &state_vec);

    const TaskProxy &get_task_proxy() const override {
        return task_proxy;
    }

    int get_num_variables() const override {
        return num_variables;
    }

    const int_packer::IntPacker &get_state_packer() const override {
        return state_packer;
    }

    /*
      Returns the state that was registered at the given ID. The ID must refer
      to a state in this registry. Do not mix IDs from from different registries.
    */
    State lookup_state(StateID id) const override;

    /*
      Like lookup_state above, but creates a state with unpacked data,
      moved in via state_values. It is the caller's responsibility that
      the unpacked data matches the state's data.
    */
    State lookup_state(StateID id, std::vector<int> &&state_values) const override;

    /*
      Returns a reference to the initial state and registers it if this was not
      done before. The result is cached internally so subsequent calls are cheap.
    */
    const State &get_initial_state() override;

    /*
      Returns the state that results from applying op to predecessor and
      registers it if this was not done before. This is an expensive operation
      as it includes duplicate checking.
    */
    State get_successor_state(const State &predecessor, const OperatorProxy &op) override;

    /*
      Returns the number of states registered so far.
    */
    size_t size() const override {
        return tree_table.size();
    }

    int get_state_size_in_bytes() const;

    void print_statistics(utils::LogProxy &log) const override;

    class const_iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = StateID;
        using difference_type = ptrdiff_t;
        using pointer = StateID *;
        using reference = StateID &;

        /*
          We intentionally omit parts of the forward iterator concept
          (e.g. default construction, copy assignment, post-increment)
          to reduce boilerplate. Supported compilers may complain about
          this, in which case we will add the missing methods.
        */

        friend class CanonicalTreeStateRegistry;
        const CanonicalTreeStateRegistry &registry;
        StateID pos;

        const_iterator(const CanonicalTreeStateRegistry &registry, size_t start)
            : registry(registry), pos(start) {
            utils::unused_variable(this->registry);
        }
public:
        const_iterator &operator++() {
            ++pos.value;
            return *this;
        }

        bool operator==(const const_iterator &rhs) const {
            assert(&registry == &rhs.registry);
            return pos == rhs.pos;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !(*this == rhs);
        }

        StateID operator*() {
            return pos;
        }

        StateID *operator->() {
            return &pos;
        }
    };
    class iterator_impl : public IStateRegistry::const_iterator {
        const CanonicalTreeStateRegistry *registry_;
        size_t idx_;
    public:
        iterator_impl(const CanonicalTreeStateRegistry *reg, size_t i) : registry_(reg), idx_(i) {}
        StateID operator*() const override { return StateID(idx_); }
        const_iterator &operator++() override { ++idx_; return *this; }
        bool operator==(const const_iterator &other) const override {
            auto p = dynamic_cast<const iterator_impl*>(&other);
            return p && registry_ == p->registry_ && idx_ == p->idx_;
        }
        std::unique_ptr<const_iterator> clone() const override {
            return std::make_unique<iterator_impl>(registry_, idx_);
        }
    };

    std::unique_ptr<IStateRegistry::const_iterator> begin() const override {
        return std::make_unique<iterator_impl>(this, 0);
    }
    std::unique_ptr<IStateRegistry::const_iterator> end() const override {
        return std::make_unique<iterator_impl>(this, size());
    }

};

#endif

#ifndef I_STATE_REGISTRY_H
#define I_STATE_REGISTRY_H

#include "abstract_task.h"
#include "axioms.h"
#include "state_id.h"

#include "algorithms/int_packer.h"
#include "algorithms/segmented_vector.h"

#include <parallel_hashmap/phmap.h>

#include <set>

#include <cstddef>
#include <vector>
#include "utils/logging.h"

enum StateRegistryType {
    PACKED = 0,
    UNPACKED = 1,
    TREE_PACKED = 2,
    TREE_UNPACKED = 3,
    FIXED_TREE_UNPACKED = 4,
    FIXED_TREE_PACKED = 5,
    HUFFMAN_TREE = 6,
    CANONICAL_TREE = 7
};

class StateRegistry : public subscriber::SubscriberService<StateRegistry>{
protected:
    TaskProxy task_proxy;

public:
    explicit StateRegistry(const TaskProxy &task_proxy): task_proxy(task_proxy) {}
    virtual ~StateRegistry() = default;

    /// Returns a reference to the underlying task proxy object.
    virtual const TaskProxy &get_task_proxy() const = 0;

    /// Number of state variables in the registry.
    virtual int get_num_variables() const = 0;

    /// Returns the IntPacker for this registry.
    virtual const int_packer::IntPacker &get_state_packer() const = 0;

    /// Returns the state associated with a given StateID.
    virtual State lookup_state(StateID id) const = 0;

    /// Returns the state given both ID and vector of state values moved-in.
    virtual State lookup_state(StateID id, std::vector<int> &&state_values) const = 0;

    /// Returns the (lazily cached) initial state.
    virtual const State &get_initial_state() = 0;

    /// Returns the result of applying op to predecessor and registering the result; includes deduplication.
    virtual State get_successor_state(const State &predecessor, const OperatorProxy &op) = 0;

    /// Number of registered states so far.
    virtual size_t size() const = 0;

    /// Print registry statistics to the given log proxy.
    virtual void print_statistics(utils::LogProxy &log) const = 0;

    // ---- Polymorphic iteration access ----

    /// Virtual base forward iterator for StateID. Must be subclassed by concrete implementations.
    class const_iterator {
    public:
        virtual ~const_iterator() = default;

        /// Dereference to StateID (pure).
        virtual StateID operator*() const = 0;

        /// Pre-increment (pure).
        virtual const_iterator& operator++() = 0;

        /// Equality comparison (pure).
        virtual bool operator==(const const_iterator& other) const = 0;

        /// Inequality comparison; implemented via equality.
        virtual bool operator!=(const const_iterator& other) const {
            return !(*this == other);
        }
        // Optional: clone (deep-copy) for multiple traversals
        virtual std::unique_ptr<const_iterator> clone() const = 0;
    };

    /**
     * @brief Returns a unique_ptr to an iterator at the beginning of all registered StateIDs.
     */
    virtual std::unique_ptr<const_iterator> begin() const = 0;

    /**
     * @brief Returns a unique_ptr to an iterator *one-past-the-end*.
     */
    virtual std::unique_ptr<const_iterator> end() const = 0;
};


#endif

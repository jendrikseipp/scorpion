#ifndef COST_SATURATION_COST_FUNCTION_REGISTRY_H
#define COST_SATURATION_COST_FUNCTION_REGISTRY_H

#include "gtl/phmap.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace cost_saturation {
using Costs = std::vector<int>;
using CostKey = uint32_t;

/*
  Assigns each distinct cost function a small dense key, so that the
  structured SCP caches can be indexed by key instead of storing full cost
  functions. Hard tasks register hundreds of thousands of cost functions.

  Cost functions are identified by an order-independent 64-bit content hash
  (sum of per-operator mixes, so callers can maintain it incrementally) and
  verified against a copy stored in one shared, append-only pool. Every
  registered function derives from the first one (the task's costs) by
  saturation, and typically only a fraction of the operator costs change,
  so the copies are stored as a bitmask of the operators that differ from
  the first function plus the bit-packed changed values. True hash
  collisions land in the (in practice empty) overflow list.
*/
class CostFunctionRegistry {
    gtl::flat_hash_map<uint64_t, CostKey> key_by_hash;
    std::vector<std::pair<uint64_t, CostKey>> overflow;
    // Costs of the first registered function; the baseline for the blobs.
    Costs baseline;
    // Packed blob of cost function key k is data[offsets[k]..offsets[k+1]).
    std::vector<uint8_t> packed_data;
    std::vector<int64_t> packed_offsets = {0};
    std::vector<uint8_t> scratch_blob;

    std::vector<unsigned int> scratch_values;

    /* Pack the operators whose cost differs from the baseline as a bitmask
       plus their values with the minimum power-of-two number of bits per
       value. Equal cost functions produce identical blobs, so blobs can be
       compared bytewise. */
    void pack(const Costs &costs, std::vector<uint8_t> &blob);

    bool equals(CostKey key, const std::vector<uint8_t> &blob) const {
        return packed_offsets[key + 1] - packed_offsets[key] ==
               static_cast<int64_t>(blob.size()) &&
               std::equal(
                   blob.begin(), blob.end(),
                   packed_data.begin() + packed_offsets[key]);
    }

public:
    // Mix an (operator, cost) pair into a 64-bit value.
    static uint64_t mix_op_cost(int op_id, int cost) {
        // splitmix64 finalizer.
        uint64_t x = static_cast<uint64_t>(op_id) * 0x9E3779B97F4A7C15ULL ^
            static_cast<uint64_t>(static_cast<unsigned int>(cost));
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    // Order-independent content hash of a cost function.
    static uint64_t compute_hash(const Costs &costs);

    int size() const {
        return packed_offsets.size() - 1;
    }

    /* Return the key of the given cost function, registering it if
       necessary. hash must equal compute_hash(costs). */
    CostKey register_or_lookup(const Costs &costs, uint64_t hash);

    void release_memory();
};
}

#endif

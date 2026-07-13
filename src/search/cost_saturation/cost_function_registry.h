#ifndef COST_SATURATION_COST_FUNCTION_REGISTRY_H
#define COST_SATURATION_COST_FUNCTION_REGISTRY_H

#include "gtl/phmap.hpp"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace cost_saturation {
using Costs = std::vector<int>;
using CostKey = uint32_t;

/*
  Append-only pool of byte blobs. Fixed-size chunks avoid the doubling
  reallocation of one big vector, whose transient old-plus-new copies
  would dominate peak memory on blob-heavy tasks.
*/
class BlobPool {
    /* Chunks double from 64 KiB up to 4 MiB, so small pools stay small
       while large pools amortize the chunk bookkeeping. */
    static constexpr size_t MIN_CHUNK_BYTES = 64 << 10;
    static constexpr size_t MAX_CHUNK_BYTES = 4 << 20;

    struct BlobRef {
        uint32_t chunk;
        uint32_t offset;
        uint32_t length;
    };

    std::vector<std::vector<uint8_t>> chunks;
    std::vector<BlobRef> refs;

public:
    int size() const {
        return refs.size();
    }

    void append(const std::vector<uint8_t> &blob) {
        if (chunks.empty() ||
            chunks.back().size() + blob.size() > chunks.back().capacity()) {
            size_t chunk_bytes = chunks.empty()
                ? MIN_CHUNK_BYTES
                : std::min(MAX_CHUNK_BYTES, 2 * chunks.back().capacity());
            chunks.emplace_back();
            chunks.back().reserve(std::max(chunk_bytes, blob.size()));
        }
        std::vector<uint8_t> &chunk = chunks.back();
        refs.push_back(
            {static_cast<uint32_t>(chunks.size() - 1),
             static_cast<uint32_t>(chunk.size()),
             static_cast<uint32_t>(blob.size())});
        chunk.insert(chunk.end(), blob.begin(), blob.end());
    }

    const uint8_t *data(int index) const {
        const BlobRef &ref = refs[index];
        return chunks[ref.chunk].data() + ref.offset;
    }

    int64_t memory_in_bytes() const {
        int64_t bytes = refs.capacity() * sizeof(BlobRef);
        for (const std::vector<uint8_t> &chunk : chunks) {
            bytes += chunk.capacity();
        }
        return bytes;
    }

    void release_memory() {
        decltype(chunks)().swap(chunks);
        decltype(refs)().swap(refs);
    }
};

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
    BlobPool blobs;
    std::vector<uint8_t> scratch_blob;
    std::vector<uint64_t> scratch_masks;
    std::vector<unsigned int> scratch_values;

    /* Pack the operators whose cost differs from the baseline as a dirty
       block bitmap, per-dirty-block bitmasks and the changed values with
       the minimum power-of-two number of bits per value. */
    void pack(const Costs &costs, std::vector<uint8_t> &blob);

    bool matches(CostKey key, const Costs &costs) const;

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
        return blobs.size();
    }

    // Sentinel returned by lookup() for unregistered cost functions.
    static constexpr CostKey NO_KEY = std::numeric_limits<CostKey>::max();

    /* Return the key of the given cost function, registering it if
       necessary. hash may be any deterministic hash of the costs; all
       calls on one registry must use the same hash function. */
    CostKey register_or_lookup(const Costs &costs, uint64_t hash);

    // Return the key of the given cost function, or NO_KEY.
    CostKey lookup(const Costs &costs, uint64_t hash) const;

    // Approximate footprint of the pool, refs and hash map.
    int64_t memory_in_bytes() const {
        return blobs.memory_in_bytes() +
               key_by_hash.size() *
               (sizeof(uint64_t) + sizeof(CostKey) + 4);
    }

    void release_memory();
};
}

#endif

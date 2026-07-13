#include "cost_function_registry.h"

#include "types.h"

#include "../utils/collections.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>

using namespace std;

namespace cost_saturation {
uint64_t CostFunctionRegistry::compute_hash(const Costs &costs) {
    uint64_t hash = 0;
    for (size_t op_id = 0; op_id < costs.size(); ++op_id) {
        hash += mix_op_cost(op_id, costs[op_id]);
    }
    return hash;
}

// Map INF to 0 and finite costs to cost + 1 to avoid a special case.
static unsigned int packed_cost_value(int cost) {
    return (cost == INF) ? 0 : static_cast<unsigned int>(cost) + 1;
}

/* Append the given values with a compile-time bit width that divides 8, so
   the loops have fixed stride. */
template<int BITS>
static void pack_values(const vector<unsigned int> &values,
                        vector<uint8_t> &blob) {
    constexpr int PER_BYTE = 8 / BITS;
    size_t offset = blob.size();
    blob.resize(offset + (values.size() + PER_BYTE - 1) / PER_BYTE, 0);
    uint8_t *bytes = blob.data() + offset;
    for (size_t i = 0; i < values.size(); ++i) {
        bytes[i / PER_BYTE] |= values[i] << (i % PER_BYTE * BITS);
    }
}

template<typename UInt>
static void pack_values_wide(const vector<unsigned int> &values,
                             vector<uint8_t> &blob) {
    size_t offset = blob.size();
    blob.resize(offset + values.size() * sizeof(UInt));
    UInt *out = reinterpret_cast<UInt *>(blob.data() + offset);
    for (size_t i = 0; i < values.size(); ++i) {
        out[i] = static_cast<UInt>(values[i]);
    }
}

void CostFunctionRegistry::pack(const Costs &costs, vector<uint8_t> &blob) {
    assert(costs.size() == baseline.size());
    size_t num_ops = costs.size();
    blob.clear();

    // Bitmask of the operators whose cost differs from the baseline.
    size_t mask_bytes = (num_ops + 7) / 8;
    blob.resize(1 + mask_bytes, 0);
    uint8_t *mask = blob.data() + 1;
    scratch_values.clear();
    unsigned int max_value = 0;
    for (size_t i = 0; i < num_ops; ++i) {
        if (costs[i] != baseline[i]) {
            mask[i / 8] |= 1 << (i % 8);
            unsigned int value = packed_cost_value(costs[i]);
            scratch_values.push_back(value);
            max_value = max(max_value, value);
        }
    }

    uint8_t bits_per_value = bit_ceil(
        static_cast<uint8_t>(bit_width(max_value)));
    blob[0] = bits_per_value;
    switch (bits_per_value) {
    case 1:
        pack_values<1>(scratch_values, blob);
        break;
    case 2:
        pack_values<2>(scratch_values, blob);
        break;
    case 4:
        pack_values<4>(scratch_values, blob);
        break;
    case 8:
        pack_values_wide<uint8_t>(scratch_values, blob);
        break;
    case 16:
        pack_values_wide<uint16_t>(scratch_values, blob);
        break;
    default:
        pack_values_wide<uint32_t>(scratch_values, blob);
        break;
    }
}

template<int BITS>
static bool values_match_dense(
    const uint8_t *mask, const uint8_t *values, const Costs &costs,
    const Costs &baseline) {
    constexpr int PER_BYTE = 8 / BITS;
    constexpr unsigned int VALUE_MASK = (1u << BITS) - 1;
    size_t value_index = 0;
    for (size_t i = 0; i < costs.size(); ++i) {
        if (mask[i / 8] & (1 << (i % 8))) {
            unsigned int stored =
                (values[value_index / PER_BYTE] >>
                 (value_index % PER_BYTE * BITS)) & VALUE_MASK;
            if (stored != packed_cost_value(costs[i])) {
                return false;
            }
            ++value_index;
        } else if (costs[i] != baseline[i]) {
            return false;
        }
    }
    return true;
}

template<typename UInt>
static bool values_match_wide(
    const uint8_t *mask, const uint8_t *values, const Costs &costs,
    const Costs &baseline) {
    size_t value_index = 0;
    for (size_t i = 0; i < costs.size(); ++i) {
        if (mask[i / 8] & (1 << (i % 8))) {
            UInt stored;
            memcpy(&stored, values + value_index * sizeof(UInt), sizeof(UInt));
            if (stored != static_cast<UInt>(packed_cost_value(costs[i]))) {
                return false;
            }
            ++value_index;
        } else if (costs[i] != baseline[i]) {
            return false;
        }
    }
    return true;
}

/* Compare the cost function against a stored blob in one mask-guided pass,
   without materializing the query's blob. */
bool CostFunctionRegistry::matches(CostKey key, const Costs &costs) const {
    const uint8_t *blob = blobs.data(key);
    uint8_t bits_per_value = blob[0];
    const uint8_t *mask = blob + 1;
    const uint8_t *values = mask + (costs.size() + 7) / 8;
    switch (bits_per_value) {
    case 1:
        return values_match_dense<1>(mask, values, costs, baseline);
    case 2:
        return values_match_dense<2>(mask, values, costs, baseline);
    case 4:
        return values_match_dense<4>(mask, values, costs, baseline);
    case 8:
        return values_match_wide<uint8_t>(mask, values, costs, baseline);
    case 16:
        return values_match_wide<uint16_t>(mask, values, costs, baseline);
    default:
        return values_match_wide<uint32_t>(mask, values, costs, baseline);
    }
}

CostKey CostFunctionRegistry::register_or_lookup(
    const Costs &costs, uint64_t hash) {
    if (baseline.empty()) {
        baseline = costs;
    }
    auto it = key_by_hash.find(hash);
    if (it == key_by_hash.end()) {
        CostKey key = size();
        pack(costs, scratch_blob);
        blobs.append(scratch_blob);
        key_by_hash.emplace(hash, key);
        return key;
    }
    if (matches(it->second, costs)) {
        return it->second;
    }
    // Hash collision: look for the cost function in the overflow list.
    for (const auto &[overflow_hash, overflow_key] : overflow) {
        if (overflow_hash == hash && matches(overflow_key, costs)) {
            return overflow_key;
        }
    }
    CostKey key = size();
    pack(costs, scratch_blob);
    blobs.append(scratch_blob);
    overflow.emplace_back(hash, key);
    return key;
}

CostKey CostFunctionRegistry::lookup(const Costs &costs, uint64_t hash) const {
    auto it = key_by_hash.find(hash);
    if (it == key_by_hash.end()) {
        return NO_KEY;
    }
    if (matches(it->second, costs)) {
        return it->second;
    }
    for (const auto &[overflow_hash, overflow_key] : overflow) {
        if (overflow_hash == hash && matches(overflow_key, costs)) {
            return overflow_key;
        }
    }
    return NO_KEY;
}

void CostFunctionRegistry::release_memory() {
    decltype(key_by_hash)().swap(key_by_hash);
    utils::release_vector_memory(overflow);
    utils::release_vector_memory(baseline);
    blobs.release_memory();
    utils::release_vector_memory(scratch_blob);
    utils::release_vector_memory(scratch_values);
}
}

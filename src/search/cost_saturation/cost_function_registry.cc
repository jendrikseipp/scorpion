#include "cost_function_registry.h"

#include "types.h"

#include "../utils/collections.h"

#include <algorithm>
#include <bit>
#include <cassert>

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

    /* First pass: bitmask of the operators whose cost differs from the
       baseline. Fixed stride, so the loop vectorizes. */
    size_t mask_bytes = (num_ops + 7) / 8;
    blob.resize(1 + mask_bytes, 0);
    uint8_t *mask = blob.data() + 1;
    size_t num_full_bytes = num_ops / 8;
    for (size_t b = 0; b < num_full_bytes; ++b) {
        uint8_t m = 0;
        for (int j = 0; j < 8; ++j) {
            m |= (costs[b * 8 + j] != baseline[b * 8 + j]) << j;
        }
        mask[b] = m;
    }
    for (size_t i = num_full_bytes * 8; i < num_ops; ++i) {
        mask[i / 8] |= (costs[i] != baseline[i]) << (i % 8);
    }

    // Second pass: gather the changed values; only visits the diffs.
    scratch_values.clear();
    unsigned int max_value = 0;
    for (size_t b = 0; b < mask_bytes; ++b) {
        unsigned int m = mask[b];
        while (m) {
            size_t i = b * 8 + countr_zero(m);
            m &= m - 1;
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

CostKey CostFunctionRegistry::register_or_lookup(
    const Costs &costs, uint64_t hash) {
    assert(hash == compute_hash(costs));
    if (baseline.empty()) {
        baseline = costs;
    }
    auto it = key_by_hash.find(hash);
    if (it == key_by_hash.end()) {
        CostKey key = size();
        pack(costs, scratch_blob);
        packed_data.insert(
            packed_data.end(), scratch_blob.begin(), scratch_blob.end());
        packed_offsets.push_back(packed_data.size());
        key_by_hash.emplace(hash, key);
        return key;
    }
    pack(costs, scratch_blob);
    if (equals(it->second, scratch_blob)) {
        return it->second;
    }
    // Hash collision: look for the cost function in the overflow list.
    for (const auto &[overflow_hash, overflow_key] : overflow) {
        if (overflow_hash == hash && equals(overflow_key, scratch_blob)) {
            return overflow_key;
        }
    }
    CostKey key = size();
    packed_data.insert(
        packed_data.end(), scratch_blob.begin(), scratch_blob.end());
    packed_offsets.push_back(packed_data.size());
    overflow.emplace_back(hash, key);
    return key;
}

void CostFunctionRegistry::release_memory() {
    decltype(key_by_hash)().swap(key_by_hash);
    utils::release_vector_memory(overflow);
    utils::release_vector_memory(baseline);
    utils::release_vector_memory(packed_data);
    utils::release_vector_memory(packed_offsets);
    utils::release_vector_memory(scratch_blob);
    utils::release_vector_memory(scratch_values);
}
}

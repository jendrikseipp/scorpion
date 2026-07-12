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

/* Pack with a compile-time width that divides 8, so the loops have fixed
   stride and vectorize. */
template<int BITS>
static void pack_values(const Costs &costs, vector<uint8_t> &blob) {
    constexpr int PER_BYTE = 8 / BITS;
    size_t num_full_bytes = costs.size() / PER_BYTE;
    size_t offset = blob.size();
    blob.resize(offset + (costs.size() + PER_BYTE - 1) / PER_BYTE, 0);
    uint8_t *bytes = blob.data() + offset;
    for (size_t b = 0; b < num_full_bytes; ++b) {
        uint8_t value = 0;
        for (int j = 0; j < PER_BYTE; ++j) {
            value |= packed_cost_value(costs[b * PER_BYTE + j]) << (j * BITS);
        }
        bytes[b] = value;
    }
    for (size_t i = num_full_bytes * PER_BYTE; i < costs.size(); ++i) {
        bytes[num_full_bytes] |=
            packed_cost_value(costs[i]) << (i % PER_BYTE * BITS);
    }
}

template<typename UInt>
static void pack_values_wide(const Costs &costs, vector<uint8_t> &blob) {
    size_t offset = blob.size();
    blob.resize(offset + costs.size() * sizeof(UInt));
    UInt *values = reinterpret_cast<UInt *>(blob.data() + offset);
    for (size_t i = 0; i < costs.size(); ++i) {
        values[i] = static_cast<UInt>(packed_cost_value(costs[i]));
    }
}

void CostFunctionRegistry::pack(const Costs &costs, vector<uint8_t> &blob) {
    unsigned int max_finite_cost = 0;
    for (int cost : costs) {
        assert(cost >= 0);
        max_finite_cost = max(
            max_finite_cost,
            (cost == INF) ? 0 : static_cast<unsigned int>(cost));
    }
    uint8_t bits_per_cost = bit_ceil(
        static_cast<uint8_t>(bit_width(max_finite_cost + 1)));

    blob.clear();
    blob.reserve(1 + (bits_per_cost * costs.size() + 7) / 8);
    blob.push_back(bits_per_cost);
    switch (bits_per_cost) {
    case 1:
        pack_values<1>(costs, blob);
        break;
    case 2:
        pack_values<2>(costs, blob);
        break;
    case 4:
        pack_values<4>(costs, blob);
        break;
    case 8:
        pack_values_wide<uint8_t>(costs, blob);
        break;
    case 16:
        pack_values_wide<uint16_t>(costs, blob);
        break;
    default:
        pack_values_wide<uint32_t>(costs, blob);
        break;
    }
}

CostKey CostFunctionRegistry::register_or_lookup(
    const Costs &costs, uint64_t hash) {
    assert(hash == compute_hash(costs));
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
    utils::release_vector_memory(packed_data);
    utils::release_vector_memory(packed_offsets);
    utils::release_vector_memory(scratch_blob);
}
}
